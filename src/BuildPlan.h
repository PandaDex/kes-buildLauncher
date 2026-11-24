#pragma once

#include "BuildProfile.h"

#include <QList>
#include <QPair>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace kbl {

struct HostInfo
{
    bool windows = false;
    QString defaultPlatform;
    QStringList platforms;
    static HostInfo current();
};

struct Issue
{
    enum Severity { Info, Warning, Error };
    Severity severity = Info;
    QString text;
};

struct PlanContext
{
    QString engineRoot;
    QString cmake = QStringLiteral("cmake");
    HostInfo host;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
};

struct Step
{
    enum Kind { Header, Process, RemovePath, WriteFile, Launch };
    Kind kind = Process;
    QString title;
    QString program;
    QStringList args;
    QStringList displayArgs;
    QString workingDir;
    QString path;
    QString profileName;
    QString buildDir;
    QString configuration;
    QString target;
    bool packaged = false;
    QString launchArgs;
    QString launchWorkingDir;
};

struct Plan
{
    QList<Step> steps;
    QList<Issue> issues;
    bool ok() const;
};

QString resolvePlatform(const BuildProfile& profile, const HostInfo& host);
QString platformId(const QString& platform);
QStringList defaultRenderers(const QString& platform);
QStringList availableRenderers(const QString& platform);
QString defaultPackageFormat(const QString& platform);
QString buildDirectory(const BuildProfile& profile, const QString& engineRoot);
QString packageDirectory(const BuildProfile& profile, const QString& engineRoot);
bool generatorIsVisualStudio(const QString& generator, const HostInfo& host);

QString pakKey(const BuildProfile& profile, const QProcessEnvironment& env, QString* error = nullptr);

struct Definition
{
    QString name;
    QString value;
    bool unset = false;
    bool secret = false;
};
QList<Definition> cmakeDefinitions(const BuildProfile& profile, const PlanContext& ctx);

QList<Issue> validate(const BuildProfile& profile, const PlanContext& ctx);
Plan makePlan(const BuildProfile& profile, const PlanContext& ctx);

QString commandLine(const Step& step);

QString findTargetArtifact(const QString& buildDir, const QString& configuration, const QString& target,
                           QString* error = nullptr);
QString readCacheValue(const QString& buildDir, const QString& name);
QString packagedExecutable(const QString& buildDir, const QString& configuration, const QString& target,
                           QString* error = nullptr);

QString findEngineRoot(const QString& start);
bool externalsPresent(const QString& engineRoot, const HostInfo& host);

}
