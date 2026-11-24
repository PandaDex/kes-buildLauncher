#include "BuildProfile.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace kbl {

namespace {

QJsonArray toArray(const QStringList& list)
{
    return QJsonArray::fromStringList(list);
}

QStringList toStringList(const QJsonValue& value)
{
    QStringList out;
    for (const QJsonValue& v : value.toArray())
        out << v.toString();
    return out;
}

bool readBool(const QJsonObject& o, const char* key, bool fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : fallback;
}

QString readString(const QJsonObject& o, const char* key, const QString& fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : fallback;
}

}

QJsonObject BuildProfile::toJson() const
{
    QJsonObject e;
    e["debugTools"] = engine.debugTools;
    e["crashReporter"] = engine.crashReporter;
    e["crashDsn"] = engine.crashDsn;
    e["engineShared"] = engine.engineShared;
    e["renderers"] = toArray(engine.renderers);
    e["rendererSelection"] = engine.rendererSelection;
    e["profiling"] = engine.profiling;
    e["profilerOnDemand"] = engine.profilerOnDemand;
    e["disabledProfilerCategories"] = toArray(engine.disabledProfilerCategories);
    e["sanitize"] = engine.sanitize;
    e["warningsAsErrors"] = engine.warningsAsErrors;
    e["buildTests"] = engine.buildTests;
    e["buildSamples"] = engine.buildSamples;

    QJsonObject p;
    p["enabled"] = package.enabled;
    p["content"] = package.content;
    p["format"] = package.format;
    p["symbols"] = package.symbols;
    p["stripSources"] = package.stripSources;
    p["vcRuntime"] = package.vcRuntime;
    p["keySource"] = package.keySource;
    p["keyEnv"] = package.keyEnv;
    if (package.keySource == QLatin1String("inline"))
        p["key"] = package.key;
    p["outputDir"] = package.outputDir;
    p["suffix"] = package.suffix;
    p["projectName"] = package.projectName;
    p["version"] = package.version;

    QJsonObject a;
    a["generator"] = advanced.generator;
    a["architecture"] = advanced.architecture;
    a["buildDir"] = advanced.buildDir;
    a["gameTarget"] = advanced.gameTarget;
    a["extraTargets"] = toArray(advanced.extraTargets);
    a["cmakeArgs"] = toArray(advanced.cmakeArgs);
    a["buildToolArgs"] = toArray(advanced.buildToolArgs);
    a["jobs"] = advanced.jobs;
    a["cleanFirst"] = advanced.cleanFirst;
    a["freshConfigure"] = advanced.freshConfigure;

    QJsonObject l;
    l["runAfterBuild"] = launch.runAfterBuild;
    l["arguments"] = launch.arguments;
    l["workingDir"] = launch.workingDir;

    QJsonObject root;
    root["formatVersion"] = 1;
    root["name"] = name;
    root["description"] = description;
    root["platform"] = platform;
    root["configuration"] = configuration;
    root["engine"] = e;
    root["package"] = p;
    root["advanced"] = a;
    root["launch"] = l;
    return root;
}

BuildProfile BuildProfile::fromJson(const QJsonObject& root)
{
    BuildProfile b;
    b.name = readString(root, "name", b.name);
    b.description = readString(root, "description", b.description);
    b.platform = readString(root, "platform", b.platform);
    b.configuration = readString(root, "configuration", b.configuration);
    if (!configurations().contains(b.configuration))
        b.configuration = QStringLiteral("Debug");

    const QJsonObject e = root.value("engine").toObject();
    b.engine.debugTools = readBool(e, "debugTools", b.engine.debugTools);
    b.engine.crashReporter = readBool(e, "crashReporter", b.engine.crashReporter);
    b.engine.crashDsn = readString(e, "crashDsn", b.engine.crashDsn);
    b.engine.engineShared = readBool(e, "engineShared", b.engine.engineShared);
    b.engine.renderers = toStringList(e.value("renderers"));
    b.engine.rendererSelection = readBool(e, "rendererSelection", b.engine.rendererSelection);
    b.engine.profiling = readBool(e, "profiling", b.engine.profiling);
    b.engine.profilerOnDemand = readBool(e, "profilerOnDemand", b.engine.profilerOnDemand);
    b.engine.disabledProfilerCategories = toStringList(e.value("disabledProfilerCategories"));
    b.engine.sanitize = readBool(e, "sanitize", b.engine.sanitize);
    b.engine.warningsAsErrors = readBool(e, "warningsAsErrors", b.engine.warningsAsErrors);
    b.engine.buildTests = readBool(e, "buildTests", b.engine.buildTests);
    b.engine.buildSamples = readBool(e, "buildSamples", b.engine.buildSamples);

    const QJsonObject p = root.value("package").toObject();
    b.package.enabled = readBool(p, "enabled", b.package.enabled);
    b.package.content = readString(p, "content", b.package.content);
    b.package.format = readString(p, "format", b.package.format);
    b.package.symbols = readString(p, "symbols", b.package.symbols);
    b.package.stripSources = readBool(p, "stripSources", b.package.stripSources);
    b.package.vcRuntime = readBool(p, "vcRuntime", b.package.vcRuntime);
    b.package.keySource = readString(p, "keySource", b.package.keySource);
    b.package.keyEnv = readString(p, "keyEnv", b.package.keyEnv);
    b.package.key = readString(p, "key", b.package.key);
    b.package.outputDir = readString(p, "outputDir", b.package.outputDir);
    b.package.suffix = readString(p, "suffix", b.package.suffix);
    b.package.projectName = readString(p, "projectName", b.package.projectName);
    b.package.version = readString(p, "version", b.package.version);

    const QJsonObject a = root.value("advanced").toObject();
    b.advanced.generator = readString(a, "generator", b.advanced.generator);
    b.advanced.architecture = readString(a, "architecture", b.advanced.architecture);
    b.advanced.buildDir = readString(a, "buildDir", b.advanced.buildDir);
    b.advanced.gameTarget = readString(a, "gameTarget", b.advanced.gameTarget);
    b.advanced.extraTargets = toStringList(a.value("extraTargets"));
    b.advanced.cmakeArgs = toStringList(a.value("cmakeArgs"));
    b.advanced.buildToolArgs = toStringList(a.value("buildToolArgs"));
    b.advanced.jobs = a.value("jobs").toInt(b.advanced.jobs);
    b.advanced.cleanFirst = readBool(a, "cleanFirst", b.advanced.cleanFirst);
    b.advanced.freshConfigure = readBool(a, "freshConfigure", b.advanced.freshConfigure);

    const QJsonObject l = root.value("launch").toObject();
    b.launch.runAfterBuild = readBool(l, "runAfterBuild", b.launch.runAfterBuild);
    b.launch.arguments = readString(l, "arguments", b.launch.arguments);
    b.launch.workingDir = readString(l, "workingDir", b.launch.workingDir);
    return b;
}

QString slugify(const QString& text)
{
    static const QRegularExpression nonWord(QStringLiteral("[^a-z0-9]+"));
    QString s = text.toLower();
    s.replace(nonWord, QStringLiteral("-"));
    while (s.startsWith('-')) s.remove(0, 1);
    while (s.endsWith('-')) s.chop(1);
    return s.isEmpty() ? QStringLiteral("profile") : s;
}

QString BuildProfile::slug() const
{
    return slugify(name);
}

const QStringList& configurations()
{
    static const QStringList list = {"Debug", "RelWithDebInfo", "Release", "MinSizeRel"};
    return list;
}

QString configurationLabel(const QString& configuration)
{
    if (configuration == QLatin1String("RelWithDebInfo")) return QStringLiteral("Development (RelWithDebInfo)");
    if (configuration == QLatin1String("MinSizeRel")) return QStringLiteral("Release, smallest (MinSizeRel)");
    return configuration;
}

const QStringList& profilerCategories()
{
    // Keep in sync with KES_PROFILE_CATEGORIES in cmake/KesProfiler.cmake.
    static const QStringList list = {"CORE", "INPUT", "WINDOW", "RENDER", "GPU", "WORLD",
                                     "ENTITIES", "PHYSICS", "ASSETS", "AUDIO", "IO", "UI"};
    return list;
}

const QStringList& packageFormats()
{
    static const QStringList list = {"zip", "folder", "tar.gz", "msixvc"};
    return list;
}

const QStringList& symbolModes()
{
    static const QStringList list = {"archive", "package", "both", "none"};
    return list;
}

}
