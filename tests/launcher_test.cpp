#include "BuildPlan.h"
#include "BuildProfile.h"
#include "ProfileStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstdio>

using namespace kbl;

static int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static QString definition(const QList<Definition>& defs, const QString& name, bool* unset = nullptr)
{
    for (const Definition& d : defs) {
        if (d.name != name) continue;
        if (unset) *unset = d.unset;
        return d.value;
    }
    return "<missing>";
}

static bool hasError(const QList<Issue>& issues)
{
    for (const Issue& i : issues)
        if (i.severity == Issue::Error) return true;
    return false;
}

static PlanContext testContext(const QString& root)
{
    PlanContext ctx;
    ctx.engineRoot = root;
    ctx.cmake = "cmake";
    ctx.host.windows = true;
    ctx.host.defaultPlatform = "Windows";
    ctx.host.platforms = {"Windows", "GDK"};
    ctx.env = QProcessEnvironment();
    return ctx;
}

static void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(data);
}

static void testJsonRoundTrip()
{
    BuildProfile p;
    p.name = "Debug Pak";
    p.configuration = "Debug";
    p.engine.crashReporter = false;
    p.engine.renderers = {"d3d11"};
    p.engine.disabledProfilerCategories = {"GPU"};
    p.package.enabled = true;
    p.package.content = "loose";
    p.package.keySource = "inline";
    p.package.key = QString(64, 'a');
    p.advanced.cmakeArgs = {"-DFOO=1"};
    p.advanced.jobs = 6;
    p.launch.runAfterBuild = true;

    const BuildProfile back = BuildProfile::fromJson(p.toJson());
    CHECK(back.name == p.name);
    CHECK(!back.engine.crashReporter);
    CHECK(back.engine.renderers == QStringList{"d3d11"});
    CHECK(back.engine.disabledProfilerCategories == QStringList{"GPU"});
    CHECK(back.package.enabled && back.package.content == "loose");
    CHECK(back.package.key == p.package.key);
    CHECK(back.advanced.cmakeArgs == p.advanced.cmakeArgs && back.advanced.jobs == 6);
    CHECK(back.launch.runAfterBuild);

    p.package.keySource = "env";
    CHECK(!p.toJson()["package"].toObject().contains("key"));

    const BuildProfile sparse = BuildProfile::fromJson(QJsonDocument::fromJson(R"({"name":"X","configuration":"Bogus"})").object());
    CHECK(sparse.configuration == "Debug");
    CHECK(sparse.engine.crashReporter && sparse.package.content == "pak");
}

static void testSlug()
{
    CHECK(slugify("Debug Pak") == "debug-pak");
    CHECK(slugify("  Release / Loose!! ") == "release-loose");
    CHECK(slugify("***") == "profile");
}

static void testDefinitions(const QString& root)
{
    PlanContext ctx = testContext(root);
    BuildProfile p;
    p.name = "Release Loose";
    p.configuration = "Release";
    p.engine.debugTools = false;
    p.engine.disabledProfilerCategories = {"AUDIO"};
    p.package.enabled = true;
    p.package.content = "loose";
    p.package.suffix = "-loose";

    const QList<Definition> d = cmakeDefinitions(p, ctx);
    CHECK(definition(d, "KES_PLATFORM") == "Windows");
    CHECK(definition(d, "CMAKE_BUILD_TYPE") == "Release");
    CHECK(definition(d, "KES_DEBUG_TOOLS") == "OFF");
    CHECK(definition(d, "KES_CRASH_REPORTER") == "ON");
    CHECK(definition(d, "KES_PACKAGE_CONTENT") == "loose");
    CHECK(definition(d, "KES_PACKAGE_SUFFIX") == "-loose");
    CHECK(definition(d, "KES_PROFILE_AUDIO") == "OFF");
    CHECK(definition(d, "KES_PROFILE_GPU") == "ON");
    CHECK(definition(d, "KES_PACKAGE_DIR") == QDir::cleanPath(root + "/out/package"));
    bool unset = false;
    definition(d, "KES_RENDERERS_WINDOWS", &unset);
    CHECK(unset);
    definition(d, "KES_PROJECT_VERSION", &unset);
    CHECK(unset);
    CHECK(definition(d, "KES_PAK_ENCRYPTION_KEY").isEmpty());

    p.platform = "GDK";
    p.engine.renderers = {"d3d11"};
    const QList<Definition> gdk = cmakeDefinitions(p, ctx);
    CHECK(definition(gdk, "KES_PLATFORM") == "GDK");
    CHECK(definition(gdk, "KES_RENDERERS_GDK", &unset) == "d3d11" && !unset);

    p.package.keySource = "env";
    p.package.keyEnv = "TEST_PAK_KEY";
    ctx.env.insert("TEST_PAK_KEY", QString(64, 'b'));
    CHECK(definition(cmakeDefinitions(p, ctx), "KES_PAK_ENCRYPTION_KEY") == QString(64, 'b'));
}

static void testValidation(const QString& root)
{
    const PlanContext ctx = testContext(root);
    BuildProfile p;
    p.name = "Valid";
    CHECK(!hasError(validate(p, ctx)));

    BuildProfile noName = p;
    noName.name = "  ";
    CHECK(hasError(validate(noName, ctx)));

    BuildProfile linux = p;
    linux.platform = "Linux";
    CHECK(hasError(validate(linux, ctx)));

    BuildProfile msix = p;
    msix.package.enabled = true;
    msix.package.format = "msixvc";
    CHECK(hasError(validate(msix, ctx)));
    msix.platform = "GDK";
    CHECK(!hasError(validate(msix, ctx)));

    BuildProfile badKey = p;
    badKey.package.keySource = "inline";
    badKey.package.key = "1234";
    CHECK(hasError(validate(badKey, ctx)));
    badKey.package.key = QString(64, 'F');
    CHECK(!hasError(validate(badKey, ctx)));

    BuildProfile missingEnv = p;
    missingEnv.package.keySource = "env";
    missingEnv.package.keyEnv = "KBL_TEST_UNSET_VARIABLE";
    CHECK(hasError(validate(missingEnv, ctx)));

    BuildProfile badVersion = p;
    badVersion.package.version = "1.0";
    CHECK(hasError(validate(badVersion, ctx)));

    BuildProfile badRenderer = p;
    badRenderer.platform = "GDK";
    badRenderer.engine.renderers = {"metal"};
    CHECK(hasError(validate(badRenderer, ctx)));
}

static void testPlan(const QString& root)
{
    const PlanContext ctx = testContext(root);
    BuildProfile p;
    p.name = "Debug Pak";
    p.configuration = "Debug";
    p.package.enabled = true;
    p.package.keySource = "inline";
    p.package.key = QString(64, 'c');
    p.advanced.jobs = 4;
    p.launch.runAfterBuild = true;

    const Plan plan = makePlan(p, ctx);
    CHECK(plan.ok());
    QList<Step::Kind> kinds;
    for (const Step& s : plan.steps) kinds << s.kind;
    CHECK(kinds == (QList<Step::Kind>{Step::Header, Step::Process, Step::WriteFile, Step::Process, Step::Process, Step::Launch}));

    const Step& configure = plan.steps[3];
    CHECK(configure.args.contains("-A") && configure.args.contains("x64"));
    CHECK(configure.args.contains("-DKES_PAK_ENCRYPTION_KEY=" + QString(64, 'c')));
    CHECK(!commandLine(configure).contains(QString(64, 'c')));
    CHECK(commandLine(configure).contains("<hidden>"));

    const Step& build = plan.steps[4];
    CHECK(build.args.contains("kes_package"));
    CHECK(build.args.join(' ').contains("--config Debug"));
    CHECK(build.args.join(' ').contains("--parallel 4"));
    CHECK(plan.steps[5].packaged);

    p.advanced.generator = "Ninja";
    const QString buildDir = buildDirectory(p, root);
    writeFile(buildDir + "/CMakeCache.txt", "CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026\n");
    const Plan switched = makePlan(p, ctx);
    int removes = 0;
    for (const Step& s : switched.steps)
        if (s.kind == Step::RemovePath) ++removes;
    CHECK(removes == 2);
    CHECK(!switched.steps[3 + removes].args.contains("-A"));
}

static void testCodeModel(const QString& root)
{
    const QString build = root + "/out/build/cm";
    const QString reply = build + "/.cmake/api/v1/reply";
    writeFile(reply + "/index-2026.json", R"({"reply":{"codemodel-v2":{"jsonFile":"codemodel-v2-1.json"}}})");
    writeFile(reply + "/codemodel-v2-1.json", R"({"configurations":[
        {"name":"Debug","targets":[{"name":"game","jsonFile":"target-game-Debug.json"}]},
        {"name":"Release","targets":[{"name":"game","jsonFile":"target-game-Release.json"}]}]})");
    writeFile(reply + "/target-game-Debug.json", R"({"artifacts":[{"path":"game/Debug/game.exe"}]})");
    writeFile(reply + "/target-game-Release.json", R"({"artifacts":[{"path":"game/Release/game.exe"}]})");

    QString error;
    CHECK(findTargetArtifact(build, "Release", "game", &error) == QDir::cleanPath(build + "/game/Release/game.exe"));
    CHECK(findTargetArtifact(build, "Debug", "nope", &error).isEmpty() && !error.isEmpty());

    writeFile(build + "/CMakeCache.txt", "KES_PACKAGE_NAME:INTERNAL=mygame\nKES_PLATFORM:STRING=Windows\n"
                                         "KES_PACKAGE_SUFFIX:STRING=-dbg\nKES_PACKAGE_DIR:PATH=" + (root + "/pkg").toUtf8() + "\n");
    CHECK(readCacheValue(build, "KES_PACKAGE_NAME") == "mygame");
    writeFile(root + "/pkg/mygame-windows-dbg/game.exe", "");
    CHECK(packagedExecutable(build, "Debug", "game", &error) == QDir(root).absoluteFilePath("pkg/mygame-windows-dbg/game.exe"));
}

static void testStore(const QString& root)
{
    const ProfileStore store(root + "/build-profiles");
    for (const BuildProfile& p : ProfileStore::defaults()) CHECK(store.save(p));
    QList<BuildProfile> loaded = store.load();
    CHECK(loaded.size() == ProfileStore::defaults().size());
    CHECK(store.uniqueName("Shipping", loaded) == "Shipping 2");

    BuildProfile renamed = loaded.first();
    const QString oldName = renamed.name;
    renamed.name = "Renamed Profile";
    CHECK(store.rename(oldName, renamed));
    loaded = store.load();
    CHECK(loaded.size() == ProfileStore::defaults().size());
    CHECK(QFile::exists(root + "/build-profiles/renamed-profile.json"));

    const PlanContext ctx = testContext(root);
    for (const BuildProfile& p : ProfileStore::defaults()) CHECK(!hasError(validate(p, ctx)));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    const QString root = temp.path();

    testJsonRoundTrip();
    testSlug();
    testDefinitions(root);
    testValidation(root);
    testPlan(root);
    testCodeModel(root);
    testStore(root);

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("build_launcher_test: all checks passed\n");
    return 0;
}
