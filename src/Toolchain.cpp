#include "Toolchain.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace kbl {

QString findCMake()
{
    const QString onPath = QStandardPaths::findExecutable("cmake");
    if (!onPath.isEmpty()) return onPath;
#ifdef Q_OS_WIN
    QStringList candidates = {QString::fromLocal8Bit(qgetenv("ProgramFiles")) + "/CMake/bin/cmake.exe"};
    const QString vswhere = QString::fromLocal8Bit(qgetenv("ProgramFiles(x86)")) + "/Microsoft Visual Studio/Installer/vswhere.exe";
    if (QFileInfo::exists(vswhere)) {
        QProcess p;
        p.start(vswhere, {"-latest", "-prerelease", "-property", "installationPath"});
        if (p.waitForFinished(5000)) {
            const QString vs = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
            if (!vs.isEmpty())
                candidates << vs + "/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe";
        }
    }
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return QDir::cleanPath(c);
#endif
    return QStringLiteral("cmake");
}

QStringList knownGenerators()
{
#ifdef Q_OS_WIN
    return {"", "Visual Studio 18 2026", "Visual Studio 17 2022", "Ninja", "Ninja Multi-Config"};
#else
    return {"", "Ninja", "Ninja Multi-Config", "Unix Makefiles"};
#endif
}

}
