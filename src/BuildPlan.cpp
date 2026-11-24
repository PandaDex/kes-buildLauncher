#include "BuildPlan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace kbl {

namespace {

const QRegularExpression& hexKeyPattern()
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return re;
}

QString onOff(bool value)
{
    return value ? QStringLiteral("ON") : QStringLiteral("OFF");
}

QString absoluteFrom(const QString& root, const QString& path)
{
    if (path.isEmpty()) return {};
    return QDir::cleanPath(QDir(root).absoluteFilePath(path));
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString quoteArg(const QString& arg)
{
    if (arg.isEmpty()) return QStringLiteral("\"\"");
    if (!arg.contains(' ') && !arg.contains('"') && !arg.contains(';')) return arg;
    QString escaped = arg;
    escaped.replace('"', QStringLiteral("\\\""));
    return '"' + escaped + '"';
}

}

HostInfo HostInfo::current()
{
    HostInfo h;
#ifdef Q_OS_WIN
    h.windows = true;
    h.defaultPlatform = QStringLiteral("Windows");
    h.platforms = {QStringLiteral("Windows"), QStringLiteral("GDK")};
#else
    h.defaultPlatform = QStringLiteral("Linux");
    h.platforms = {QStringLiteral("Linux")};
#endif
    return h;
}

bool Plan::ok() const
{
    for (const Issue& issue : issues)
        if (issue.severity == Issue::Error) return false;
    return true;
}

QString resolvePlatform(const BuildProfile& profile, const HostInfo& host)
{
    if (profile.platform.isEmpty() || profile.platform == QLatin1String("Host"))
        return host.defaultPlatform;
    return profile.platform;
}

QString platformId(const QString& platform)
{
    return platform.toUpper();
}

QStringList defaultRenderers(const QString& platform)
{
    if (platform == QLatin1String("Windows")) return {"vulkan", "d3d11"};
    if (platform == QLatin1String("GDK")) return {"d3d11"};
    return {"vulkan"};
}

QStringList availableRenderers(const QString& platform)
{
    if (platform == QLatin1String("Windows") || platform == QLatin1String("GDK")) return {"vulkan", "d3d11"};
    return {"vulkan"};
}

QString defaultPackageFormat(const QString& platform)
{
    if (platform == QLatin1String("GDK")) return QStringLiteral("msixvc");
    if (platform == QLatin1String("Linux")) return QStringLiteral("tar.gz");
    return QStringLiteral("zip");
}

QString buildDirectory(const BuildProfile& profile, const QString& engineRoot)
{
    if (!profile.advanced.buildDir.isEmpty()) return absoluteFrom(engineRoot, profile.advanced.buildDir);
    return QDir::cleanPath(engineRoot + QStringLiteral("/out/build/profile-") + profile.slug());
}

QString packageDirectory(const BuildProfile& profile, const QString& engineRoot)
{
    if (!profile.package.outputDir.isEmpty()) return absoluteFrom(engineRoot, profile.package.outputDir);
    return QDir::cleanPath(engineRoot + QStringLiteral("/out/package"));
}

bool generatorIsVisualStudio(const QString& generator, const HostInfo& host)
{
    if (generator.isEmpty()) return host.windows;
    return generator.startsWith(QLatin1String("Visual Studio"));
}

QString pakKey(const BuildProfile& profile, const QProcessEnvironment& env, QString* error)
{
    const PackageOptions& p = profile.package;
    if (p.keySource == QLatin1String("env")) {
        const QString value = env.value(p.keyEnv).trimmed();
        if (value.isEmpty() && error)
            *error = QStringLiteral("Environment variable %1 is not set").arg(p.keyEnv);
        return value;
    }
    if (p.keySource == QLatin1String("inline")) return p.key.trimmed();
    return {};
}

QList<Definition> cmakeDefinitions(const BuildProfile& profile, const PlanContext& ctx)
{
    const EngineOptions& e = profile.engine;
    const PackageOptions& p = profile.package;
    const QString platform = resolvePlatform(profile, ctx.host);
    const QString id = platformId(platform);

    QList<Definition> d;
    auto set = [&d](const QString& name, const QString& value, bool secret = false) {
        d.append({name, value, false, secret});
    };
    auto setOrUnset = [&d](const QString& name, const QString& value) {
        d.append({name, value, value.isEmpty(), false});
    };

    set("KES_PLATFORM", platform);
    set("CMAKE_BUILD_TYPE", profile.configuration);
    set("KES_DEBUG_TOOLS", onOff(e.debugTools));
    set("KES_CRASH_REPORTER", onOff(e.crashReporter));
    set("KES_CRASH_DSN", e.crashDsn);
    set("KES_ENGINE_SHARED", onOff(e.engineShared));
    setOrUnset("KES_RENDERERS_" + id, e.renderers.join(';'));
    set("KES_ALLOW_RENDERER_SELECTION", onOff(e.rendererSelection));
    set("KES_PROFILING", onOff(e.profiling));
    set("KES_PROFILER_ON_DEMAND", onOff(e.profilerOnDemand));
    for (const QString& category : profilerCategories())
        set("KES_PROFILE_" + category, onOff(!e.disabledProfilerCategories.contains(category)));
    set("KES_SANITIZE", onOff(e.sanitize));
    set("KES_WARNINGS_AS_ERRORS", onOff(e.warningsAsErrors));
    set("KES_BUILD_TESTS", onOff(e.buildTests));
    set("KES_BUILD_SAMPLES", onOff(e.buildSamples));

    set("KES_PACKAGE_CONTENT", p.content);
    set("KES_PACKAGE_SYMBOLS", p.symbols);
    set("KES_PACKAGE_SUFFIX", p.suffix);
    set("KES_PACKAGE_STRIP_SOURCES", onOff(p.stripSources));
    set("KES_PACKAGE_VC_RUNTIME", onOff(p.vcRuntime));
    setOrUnset("KES_PACKAGE_FORMAT_" + id, p.format);
    set("KES_PACKAGE_DIR", packageDirectory(profile, ctx.engineRoot));
    set("KES_PAK_ENCRYPTION_KEY", pakKey(profile, ctx.env), true);
    setOrUnset("KES_PROJECT_NAME", p.projectName);
    setOrUnset("KES_PROJECT_VERSION", p.version);
    return d;
}

QList<Issue> validate(const BuildProfile& profile, const PlanContext& ctx)
{
    QList<Issue> issues;
    auto add = [&issues](Issue::Severity s, const QString& text) { issues.append({s, text}); };
    const QString platform = resolvePlatform(profile, ctx.host);
    const PackageOptions& p = profile.package;
    const EngineOptions& e = profile.engine;

    if (profile.name.trimmed().isEmpty())
        add(Issue::Error, "The profile needs a name.");
    if (!ctx.host.platforms.contains(platform))
        add(Issue::Error, QString("Platform %1 can't be built on this machine (%2).").arg(platform, ctx.host.platforms.join(", ")));
    if (!configurations().contains(profile.configuration))
        add(Issue::Error, QString("Unknown configuration '%1'.").arg(profile.configuration));
    if (profile.advanced.gameTarget.trimmed().isEmpty())
        add(Issue::Error, "The game target can't be empty.");

    const QStringList known = availableRenderers(platform);
    for (const QString& r : e.renderers)
        if (!known.contains(r))
            add(Issue::Error, QString("Renderer '%1' isn't available on %2.").arg(r, platform));
    if (e.profilerOnDemand && !e.profiling)
        add(Issue::Info, "Profiler on-demand has no effect while profiling is off.");
    if (e.sanitize && e.crashReporter)
        add(Issue::Info, "Sanitizers turn the crash reporter off (ASan owns the crash signals).");
    if (e.sanitize && ctx.host.windows && profile.configuration != QLatin1String("Debug"))
        add(Issue::Info, "MSVC ASan is most useful in Debug builds.");
    if (!e.crashReporter && !e.crashDsn.isEmpty())
        add(Issue::Info, "The crash DSN is ignored while the crash reporter is off.");

    if (!p.version.isEmpty() && !QRegularExpression("^\\d+\\.\\d+\\.\\d+\\.\\d+$").match(p.version).hasMatch())
        add(Issue::Error, "Version must look like 1.0.0.0.");
    if (!p.suffix.isEmpty() && QRegularExpression("[\\\\/:*?\"<>|]").match(p.suffix).hasMatch())
        add(Issue::Error, "The package suffix can't contain \\ / : * ? \" < > |");

    const bool usesKey = p.keySource != QLatin1String("none");
    if (usesKey) {
        QString keyError;
        const QString key = pakKey(profile, ctx.env, &keyError);
        if (!keyError.isEmpty())
            add(Issue::Error, keyError + '.');
        else if (!hexKeyPattern().match(key).hasMatch())
            add(Issue::Error, "The pak encryption key must be 64 hex characters.");
        if (p.keySource == QLatin1String("inline"))
            add(Issue::Warning, "The pak key is stored in the profile file as plain text. Don't commit it; prefer an environment variable.");
    }

    if (p.enabled) {
        const QString format = p.format.isEmpty() ? defaultPackageFormat(platform) : p.format;
        if (format == QLatin1String("msixvc") && platform != QLatin1String("GDK"))
            add(Issue::Error, "msixvc packages need the GDK platform.");
        if (format == QLatin1String("tar.gz") && ctx.host.windows)
            add(Issue::Info, "tar.gz on Windows works, but zip is what players expect.");
        if (p.content == QLatin1String("loose") && usesKey)
            add(Issue::Warning, "Loose content isn't encrypted; the key is still compiled into the engine.");
        if (p.vcRuntime && !ctx.host.windows)
            add(Issue::Info, "Bundling the MSVC runtime only applies to Windows.");
        if (p.vcRuntime && profile.configuration == QLatin1String("Debug"))
            add(Issue::Warning, "Debug builds bundle the debug MSVC runtime, which Microsoft doesn't allow redistributing. Fine for internal testing.");
        if (e.profiling)
            add(Issue::Info, "Profiling builds collect Tracy data; use a non-profiling profile for releases.");
        if (p.symbols == QLatin1String("package") || p.symbols == QLatin1String("both"))
            add(Issue::Info, "Debug symbols ship inside the package.");
    } else if (usesKey) {
        add(Issue::Info, "The key only matters for packaged paks; development builds read loose files.");
    }

    if (ctx.host.windows && !profile.advanced.generator.isEmpty() && !generatorIsVisualStudio(profile.advanced.generator, ctx.host) &&
        !ctx.env.contains("VCINSTALLDIR"))
        add(Issue::Warning, "Ninja and Makefile generators need the Visual Studio environment; start the launcher from a "
                            "Developer Command Prompt, or use a Visual Studio generator.");
    if (!externalsPresent(ctx.engineRoot, ctx.host))
        add(Issue::Info, "External binaries (slangc, ...) are missing; they'll be downloaded first.");
    return issues;
}

QString findEngineRoot(const QString& start)
{
    QDir dir(QFileInfo(start).isDir() ? start : QFileInfo(start).absolutePath());
    do {
        if (QFileInfo::exists(dir.absoluteFilePath("CMakeLists.txt")) &&
            QFileInfo::exists(dir.absoluteFilePath("cmake/KesPackage.cmake")))
            return dir.absolutePath();
    } while (dir.cdUp());
    return {};
}

bool externalsPresent(const QString& engineRoot, const HostInfo& host)
{
    const QString slangc = host.windows ? QStringLiteral("/external/slang/win64/bin/slangc.exe")
                                        : QStringLiteral("/external/slang/linux64/bin/slangc");
    return QFileInfo::exists(engineRoot + slangc);
}

Plan makePlan(const BuildProfile& profile, const PlanContext& ctx)
{
    Plan plan;
    plan.issues = validate(profile, ctx);
    if (!plan.ok()) return plan;

    const QString buildDir = buildDirectory(profile, ctx.engineRoot);
    const AdvancedOptions& a = profile.advanced;
    const bool vs = generatorIsVisualStudio(a.generator, ctx.host);

    Step header;
    header.kind = Step::Header;
    header.title = profile.name;
    header.profileName = profile.name;
    plan.steps << header;

    if (!externalsPresent(ctx.engineRoot, ctx.host)) {
        Step download;
        download.title = "Download external binaries";
        download.workingDir = ctx.engineRoot;
        if (ctx.host.windows) {
            download.program = "powershell";
            download.args = QStringList{"-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                        QDir::toNativeSeparators(ctx.engineRoot + "/external/download_external.ps1")};
        } else {
            download.program = "bash";
            download.args = QStringList{ctx.engineRoot + "/external/download_external.sh"};
        }
        plan.steps << download;
    }

    const QString cachedGenerator = readCacheValue(buildDir, "CMAKE_GENERATOR");
    const QString cachedArch = readCacheValue(buildDir, "CMAKE_GENERATOR_PLATFORM");
    const bool generatorChanged = !a.generator.isEmpty() && !cachedGenerator.isEmpty() && cachedGenerator != a.generator;
    const bool archChanged = vs && !cachedGenerator.isEmpty() && !a.architecture.isEmpty() && cachedArch != a.architecture;
    if (generatorChanged || archChanged) {
        for (const QString& name : {QStringLiteral("CMakeCache.txt"), QStringLiteral("CMakeFiles")}) {
            Step remove;
            remove.kind = Step::RemovePath;
            remove.title = QString("Remove %1 (generator or architecture changed)").arg(name);
            remove.path = buildDir + '/' + name;
            plan.steps << remove;
        }
    }

    Step query;
    query.kind = Step::WriteFile;
    query.title = "Request the CMake code model";
    query.path = buildDir + "/.cmake/api/v1/query/codemodel-v2";
    plan.steps << query;

    Step configure;
    configure.title = "Configure";
    configure.program = ctx.cmake;
    configure.workingDir = ctx.engineRoot;
    configure.args << "-S" << ctx.engineRoot << "-B" << buildDir;
    if (!a.generator.isEmpty()) configure.args << "-G" << a.generator;
    if (vs && !a.architecture.isEmpty()) configure.args << "-A" << a.architecture;
    if (a.freshConfigure) configure.args << "--fresh";
    configure.displayArgs = configure.args;
    for (const Definition& d : cmakeDefinitions(profile, ctx)) {
        if (d.unset) {
            configure.args << "-U" << d.name;
            configure.displayArgs << "-U" << d.name;
        } else {
            configure.args << QString("-D%1=%2").arg(d.name, d.value);
            configure.displayArgs << QString("-D%1=%2").arg(d.name, d.secret && !d.value.isEmpty() ? QStringLiteral("<hidden>") : d.value);
        }
    }
    configure.args << a.cmakeArgs;
    configure.displayArgs << a.cmakeArgs;
    plan.steps << configure;

    Step build;
    build.title = profile.package.enabled ? "Build and package" : "Build";
    build.program = ctx.cmake;
    build.workingDir = ctx.engineRoot;
    build.args << "--build" << buildDir << "--config" << profile.configuration;
    build.args << "--target" << (profile.package.enabled ? QStringLiteral("kes_package") : a.gameTarget) << a.extraTargets;
    if (a.jobs > 0) build.args << "--parallel" << QString::number(a.jobs);
    else build.args << "--parallel";
    if (a.cleanFirst) build.args << "--clean-first";
    if (!a.buildToolArgs.isEmpty()) build.args << "--" << a.buildToolArgs;
    build.displayArgs = build.args;
    plan.steps << build;

    if (profile.launch.runAfterBuild) {
        Step launch;
        launch.kind = Step::Launch;
        launch.title = "Launch";
        launch.buildDir = buildDir;
        launch.configuration = profile.configuration;
        launch.target = a.gameTarget;
        launch.packaged = profile.package.enabled;
        launch.launchArgs = profile.launch.arguments;
        launch.launchWorkingDir = absoluteFrom(ctx.engineRoot, profile.launch.workingDir);
        plan.steps << launch;
    }
    return plan;
}

QString commandLine(const Step& step)
{
    switch (step.kind) {
    case Step::Header: return "# " + step.title;
    case Step::RemovePath: return "remove " + quoteArg(QDir::toNativeSeparators(step.path));
    case Step::WriteFile: return "touch " + quoteArg(QDir::toNativeSeparators(step.path));
    case Step::Launch: return QString("run %1 %2").arg(step.packaged ? "<packaged exe>" : "<built exe>", step.launchArgs).trimmed();
    case Step::Process: break;
    }
    QStringList parts{quoteArg(step.program)};
    for (const QString& arg : step.displayArgs.isEmpty() ? step.args : step.displayArgs)
        parts << quoteArg(arg);
    return parts.join(' ');
}

QString readCacheValue(const QString& buildDir, const QString& name)
{
    QFile f(buildDir + "/CMakeCache.txt");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString prefix = name + ':';
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (!line.startsWith(prefix)) continue;
        const qsizetype eq = line.indexOf('=');
        return eq < 0 ? QString() : line.mid(eq + 1);
    }
    return {};
}

QString findTargetArtifact(const QString& buildDir, const QString& configuration, const QString& target, QString* error)
{
    auto fail = [error](const QString& text) {
        if (error) *error = text;
        return QString();
    };
    const QDir reply(buildDir + "/.cmake/api/v1/reply");
    QStringList indexes = reply.entryList({"index-*.json"}, QDir::Files, QDir::Name);
    if (indexes.isEmpty()) return fail("No CMake file API reply in " + reply.path());

    const QJsonObject index = readJson(reply.absoluteFilePath(indexes.last()));
    const QString codemodelFile = index["reply"].toObject()["codemodel-v2"].toObject()["jsonFile"].toString();
    if (codemodelFile.isEmpty()) return fail("The CMake reply has no code model");

    const QJsonArray configs = readJson(reply.absoluteFilePath(codemodelFile))["configurations"].toArray();
    QJsonObject config;
    for (const QJsonValue& c : configs)
        if (c.toObject()["name"].toString() == configuration) config = c.toObject();
    if (config.isEmpty() && configs.size() == 1) config = configs.first().toObject();
    if (config.isEmpty()) return fail(QString("Configuration %1 isn't in the code model").arg(configuration));

    for (const QJsonValue& t : config["targets"].toArray()) {
        if (t.toObject()["name"].toString() != target) continue;
        const QJsonObject targetJson = readJson(reply.absoluteFilePath(t.toObject()["jsonFile"].toString()));
        const QJsonArray artifacts = targetJson["artifacts"].toArray();
        if (artifacts.isEmpty()) return fail(QString("Target %1 has no artifacts").arg(target));
        const QString path = artifacts.first().toObject()["path"].toString();
        return QDir::cleanPath(QDir(buildDir).absoluteFilePath(path));
    }
    return fail(QString("Target %1 isn't in the code model").arg(target));
}

QString packagedExecutable(const QString& buildDir, const QString& configuration, const QString& target, QString* error)
{
    const QString built = findTargetArtifact(buildDir, configuration, target, error);
    if (built.isEmpty()) return {};
    const QString name = readCacheValue(buildDir, "KES_PACKAGE_NAME");
    const QString platform = readCacheValue(buildDir, "KES_PLATFORM").toLower();
    const QString suffix = readCacheValue(buildDir, "KES_PACKAGE_SUFFIX");
    const QString packageDir = readCacheValue(buildDir, "KES_PACKAGE_DIR");
    const QString exe = QDir(packageDir).absoluteFilePath(QString("%1-%2%3/%4").arg(name, platform, suffix, QFileInfo(built).fileName()));
    if (!QFileInfo::exists(exe)) {
        if (error) *error = "Packaged executable not found: " + exe;
        return {};
    }
    return exe;
}

}
