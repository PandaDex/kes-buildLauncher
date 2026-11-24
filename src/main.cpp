#include "BuildPlan.h"
#include "KesStyle.h"
#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("kesBuildLauncher");
    QApplication::setOrganizationName("KES Engine");
    kes::style::apply(app);
    QApplication::setWindowIcon(QIcon(":/icons/icon.png"));

    QSettings settings;
    QString root;
    const QStringList args = app.arguments();
    if (args.size() > 1) root = kbl::findEngineRoot(QDir::fromNativeSeparators(args.at(1)));
    if (root.isEmpty()) root = kbl::findEngineRoot(QCoreApplication::applicationDirPath());
    if (root.isEmpty()) root = kbl::findEngineRoot(QDir::currentPath());
    if (root.isEmpty()) root = kbl::findEngineRoot(settings.value("engineRoot").toString());
    while (root.isEmpty()) {
        const QString picked = QFileDialog::getExistingDirectory(nullptr, "Select the KES engine folder");
        if (picked.isEmpty()) return 0;
        root = kbl::findEngineRoot(picked);
        if (root.isEmpty())
            QMessageBox::warning(nullptr, "Build Launcher", "That folder isn't a KES engine checkout (no cmake/KesPackage.cmake).");
    }
    settings.setValue("engineRoot", root);

    kbl::MainWindow w(root);
    w.show();
    return app.exec();
}
