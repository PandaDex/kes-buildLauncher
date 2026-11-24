#include "BuildPlan.h"
#include "BuildRunner.h"
#include "ProfileStore.h"
#include "Toolchain.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

#include <cstdio>

using namespace kbl;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("kesbuild");

    QCommandLineParser parser;
    parser.setApplicationDescription("Builds and packages KES games from build profiles (build-profiles/*.json).");
    parser.addHelpOption();
    parser.addPositionalArgument("profiles", "Profile names or .json files, run in order.", "[profile...]");
    QCommandLineOption listOpt("list", "List the profiles.");
    QCommandLineOption dryRunOpt("dry-run", "Print the commands without running them.");
    QCommandLineOption noLaunchOpt("no-launch", "Don't start the game after building.");
    QCommandLineOption rootOpt("root", "Engine root (default: found from the working directory).", "dir");
    QCommandLineOption keepGoingOpt("keep-going", "Continue with the next profile when one fails.");
    QCommandLineOption writeDefaultsOpt("write-defaults", "Write the default profiles into build-profiles/ (existing files are kept).");
    parser.addOptions({listOpt, dryRunOpt, noLaunchOpt, rootOpt, keepGoingOpt, writeDefaultsOpt});
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    QString root = parser.isSet(rootOpt) ? QDir(parser.value(rootOpt)).absolutePath() : findEngineRoot(QDir::currentPath());
    if (root.isEmpty()) root = findEngineRoot(QCoreApplication::applicationDirPath());
    if (root.isEmpty()) {
        err << "Engine root not found; run inside the engine folder or pass --root.\n";
        return 2;
    }

    const ProfileStore store(root + "/build-profiles");
    if (parser.isSet(writeDefaultsOpt)) {
        for (const BuildProfile& p : ProfileStore::defaults()) {
            if (QFileInfo::exists(store.filePath(p))) continue;
            QString error;
            if (!store.save(p, &error)) {
                err << "Couldn't write " << store.filePath(p) << ": " << error << "\n";
                return 1;
            }
            out << "Wrote " << QDir::toNativeSeparators(store.filePath(p)) << "\n";
        }
    }

    QStringList loadErrors;
    const QList<BuildProfile> profiles = store.load(&loadErrors);
    for (const QString& e : loadErrors) err << "warning: " << e << "\n";

    if (parser.isSet(listOpt)) {
        for (const BuildProfile& p : profiles)
            out << p.name << "  [" << p.slug() << "]  " << configurationLabel(p.configuration)
                << (p.package.enabled ? ", packaged" : "") << "\n    " << p.description << "\n";
        return 0;
    }

    const QStringList wanted = parser.positionalArguments();
    if (wanted.isEmpty()) {
        if (parser.isSet(writeDefaultsOpt)) return 0;
        parser.showHelp(1);
    }

    PlanContext ctx;
    ctx.engineRoot = root;
    ctx.host = HostInfo::current();
    ctx.cmake = findCMake();

    QList<QList<Step>> jobs;
    bool anyError = false;
    for (const QString& name : wanted) {
        BuildProfile profile;
        bool found = false;
        if (name.endsWith(".json", Qt::CaseInsensitive)) {
            QString error;
            found = loadProfileFile(name, profile, &error);
            if (!found) err << error << "\n";
        } else {
            for (const BuildProfile& p : profiles) {
                if (p.name.compare(name, Qt::CaseInsensitive) == 0 || p.slug() == slugify(name)) {
                    profile = p;
                    found = true;
                    break;
                }
            }
            if (!found) err << "No profile named '" << name << "' in " << QDir::toNativeSeparators(store.directory()) << "\n";
        }
        if (!found) {
            anyError = true;
            continue;
        }
        if (parser.isSet(noLaunchOpt)) profile.launch.runAfterBuild = false;

        const Plan plan = makePlan(profile, ctx);
        for (const Issue& issue : plan.issues) {
            const char* tag = issue.severity == Issue::Error ? "error" : issue.severity == Issue::Warning ? "warning" : "note";
            err << profile.name << ": " << tag << ": " << issue.text << "\n";
        }
        if (!plan.ok()) {
            anyError = true;
            continue;
        }
        jobs << plan.steps;
    }
    err.flush();
    if (anyError && !parser.isSet(keepGoingOpt)) return 1;

    if (parser.isSet(dryRunOpt)) {
        for (const QList<Step>& steps : jobs)
            for (const Step& s : steps)
                out << commandLine(s) << "\n";
        return anyError ? 1 : 0;
    }

    int failures = anyError ? 1 : 0;
    BuildRunner runner;
    QObject::connect(&runner, &BuildRunner::output, [&](const QString& line, BuildRunner::Stream stream) {
        QTextStream& s = stream == BuildRunner::Stream::Err ? err : out;
        s << line << "\n";
        s.flush();
    });
    QObject::connect(&runner, &BuildRunner::stepStarted, [&](int, int, const Step& step) {
        out << (step.kind == Step::Header ? "\n=== " : "== ") << step.title << "\n";
        out.flush();
    });
    QObject::connect(&runner, &BuildRunner::profileFinished, [&](const QString& name, bool ok, qint64 ms) {
        out << "=== " << name << (ok ? " succeeded" : " FAILED") << QString(" in %1 s\n").arg(ms / 1000.0, 0, 'f', 1);
        out.flush();
    });

    for (const QList<Step>& steps : jobs) {
        bool ok = false;
        QEventLoop loop;
        QObject::connect(&runner, &BuildRunner::finished, &loop, [&](bool success, bool) {
            ok = success;
            loop.quit();
        });
        runner.start(steps);
        loop.exec();
        QObject::disconnect(&runner, &BuildRunner::finished, &loop, nullptr);
        if (!ok) {
            ++failures;
            if (!parser.isSet(keepGoingOpt)) break;
        }
    }
    return failures ? 1 : 0;
}
