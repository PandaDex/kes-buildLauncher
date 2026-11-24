#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace kbl {

struct EngineOptions
{
    bool debugTools = true;
    bool crashReporter = true;
    QString crashDsn;
    bool engineShared = true;
    QStringList renderers;
    bool rendererSelection = true;
    bool profiling = false;
    bool profilerOnDemand = false;
    QStringList disabledProfilerCategories;
    bool sanitize = false;
    bool warningsAsErrors = false;
    bool buildTests = false;
    bool buildSamples = false;
};

struct PackageOptions
{
    bool enabled = false;
    QString content = QStringLiteral("pak");
    QString format;
    QString symbols = QStringLiteral("archive");
    bool stripSources = true;
    bool vcRuntime = false;
    QString keySource = QStringLiteral("none");
    QString keyEnv = QStringLiteral("KES_PAK_KEY");
    QString key;
    QString outputDir;
    QString suffix;
    QString projectName;
    QString version;
};

struct AdvancedOptions
{
    QString generator;
    QString architecture = QStringLiteral("x64");
    QString buildDir;
    QString gameTarget = QStringLiteral("game");
    QStringList extraTargets;
    QStringList cmakeArgs;
    QStringList buildToolArgs;
    int jobs = 0;
    bool cleanFirst = false;
    bool freshConfigure = false;
};

struct LaunchOptions
{
    bool runAfterBuild = false;
    QString arguments;
    QString workingDir;
};

struct BuildProfile
{
    QString name;
    QString description;
    QString platform = QStringLiteral("Host");
    QString configuration = QStringLiteral("Debug");
    EngineOptions engine;
    PackageOptions package;
    AdvancedOptions advanced;
    LaunchOptions launch;

    QJsonObject toJson() const;
    static BuildProfile fromJson(const QJsonObject& json);
    QString slug() const;
};

QString slugify(const QString& text);

const QStringList& configurations();
const QStringList& profilerCategories();
const QStringList& packageFormats();
const QStringList& symbolModes();
QString configurationLabel(const QString& configuration);

}
