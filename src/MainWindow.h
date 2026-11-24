#pragma once

#include "BuildPlan.h"
#include "BuildProfile.h"
#include "BuildRunner.h"
#include "ProfileStore.h"

#include <QMainWindow>
#include <memory>

class QAction;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QTimer;

namespace kbl {

class ProfileEditor;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString& engineRoot, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void reloadProfiles(const QString& select = {});
    void refreshList();
    void selectProfile(int row);
    void profileEdited();
    void saveCurrent();
    void newProfile();
    void duplicateProfile();
    void deleteProfile();
    void buildProfiles(const QList<int>& rows);
    void appendLog(const QString& line, BuildRunner::Stream stream);
    void updateActions();
    void openFolder(const QString& path);
    QList<int> selectedRows() const;

    QString m_engineRoot;
    PlanContext m_ctx;
    std::unique_ptr<ProfileStore> m_store;
    QList<BuildProfile> m_profiles;
    int m_current = -1;
    QString m_savedName;
    bool m_loadingProfile = false;

    QListWidget* m_list = nullptr;
    ProfileEditor* m_editor = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QLabel* m_stepLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QTimer* m_saveTimer = nullptr;
    BuildRunner* m_runner = nullptr;

    QAction* m_newAction = nullptr;
    QAction* m_duplicateAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_buildAction = nullptr;
    QAction* m_buildSelectedAction = nullptr;
    QAction* m_cancelAction = nullptr;
    QAction* m_openPackageAction = nullptr;
    QAction* m_openBuildAction = nullptr;
};

}
