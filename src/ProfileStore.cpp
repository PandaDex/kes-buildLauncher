#include "ProfileStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace kbl {

ProfileStore::ProfileStore(QString directory)
    : m_directory(std::move(directory))
{
}

bool loadProfileFile(const QString& path, BuildProfile& out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("%1: %2").arg(path, f.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error) *error = QString("%1: %2").arg(path, parseError.errorString());
        return false;
    }
    out = BuildProfile::fromJson(doc.object());
    if (out.name.isEmpty()) out.name = QFileInfo(path).completeBaseName();
    return true;
}

QList<BuildProfile> ProfileStore::load(QStringList* errors) const
{
    QList<BuildProfile> profiles;
    const QDir dir(m_directory);
    for (const QString& file : dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
        BuildProfile profile;
        QString error;
        if (loadProfileFile(dir.absoluteFilePath(file), profile, &error))
            profiles << profile;
        else if (errors)
            *errors << error;
    }
    std::sort(profiles.begin(), profiles.end(), [](const BuildProfile& a, const BuildProfile& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return profiles;
}

QString ProfileStore::filePath(const BuildProfile& profile) const
{
    return QDir(m_directory).absoluteFilePath(profile.slug() + ".json");
}

bool ProfileStore::save(const BuildProfile& profile, QString* error) const
{
    QDir().mkpath(m_directory);
    QSaveFile f(filePath(profile));
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(profile.toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool ProfileStore::remove(const BuildProfile& profile) const
{
    return QFile::remove(filePath(profile));
}

bool ProfileStore::rename(const QString& oldName, const BuildProfile& profile, QString* error) const
{
    BuildProfile old;
    old.name = oldName;
    if (!save(profile, error)) return false;
    if (old.slug() != profile.slug()) QFile::remove(filePath(old));
    return true;
}

QString ProfileStore::uniqueName(const QString& base, const QList<BuildProfile>& existing) const
{
    auto taken = [&existing](const QString& name) {
        return std::any_of(existing.begin(), existing.end(), [&name](const BuildProfile& p) {
            return p.slug() == slugify(name);
        });
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        const QString candidate = QString("%1 %2").arg(base).arg(i);
        if (!taken(candidate)) return candidate;
    }
}

QList<BuildProfile> ProfileStore::defaults()
{
    QList<BuildProfile> list;

    BuildProfile dev;
    dev.name = "Development";
    dev.description = "Debug build with loose content next to the exe, for day-to-day work.";
    dev.configuration = "Debug";
    dev.engine.buildTests = true;
    dev.engine.buildSamples = true;
    dev.launch.runAfterBuild = true;
    list << dev;

    BuildProfile shipping;
    shipping.name = "Shipping";
    shipping.description = "What package.ps1 / package.sh make: Release, paks, crash reporter, no debug tools.";
    shipping.configuration = "Release";
    shipping.engine.debugTools = false;
    shipping.package.enabled = true;
    list << shipping;

    BuildProfile debugPak;
    debugPak.name = "Debug Pak";
    debugPak.description = "Debug build packaged with paks, without the crash reporter, symbols next to the exe.";
    debugPak.configuration = "Debug";
    debugPak.engine.crashReporter = false;
    debugPak.package.enabled = true;
    debugPak.package.format = "folder";
    debugPak.package.symbols = "package";
    debugPak.package.vcRuntime = true;
    debugPak.package.suffix = "-debug-pak";
    list << debugPak;

    BuildProfile releaseLoose;
    releaseLoose.name = "Release Loose";
    releaseLoose.description = "Release package with loose content files, easy to patch and inspect.";
    releaseLoose.configuration = "Release";
    releaseLoose.engine.debugTools = false;
    releaseLoose.package.enabled = true;
    releaseLoose.package.content = "loose";
    releaseLoose.package.format = "folder";
    releaseLoose.package.suffix = "-loose";
    list << releaseLoose;

    BuildProfile profile;
    profile.name = "Profiling";
    profile.description = "Development (RelWithDebInfo) package with Tracy, for profiling outside the editor.";
    profile.configuration = "RelWithDebInfo";
    profile.engine.profiling = true;
    profile.package.enabled = true;
    profile.package.format = "folder";
    profile.package.symbols = "package";
    profile.package.suffix = "-profiling";
    list << profile;

    return list;
}

}
