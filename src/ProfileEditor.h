#pragma once

#include "BuildPlan.h"
#include "BuildProfile.h"

#include <QHash>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QToolButton;

namespace kbl {

class ProfileEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileEditor(QWidget* parent = nullptr);

    void setContext(const PlanContext& ctx);
    void setProfile(const BuildProfile& profile);
    BuildProfile profile() const;

signals:
    void edited();

private:
    QWidget* makeGeneralTab();
    QWidget* makeEngineTab();
    QWidget* makePackageTab();
    QWidget* makeAdvancedTab();
    QWidget* makePreviewTab();
    void wire(QObject* widget);
    void changed();
    void refresh();
    void fillRenderers(const QString& platform, const QStringList& order, const QStringList& enabled);
    QString currentPlatform() const;

    PlanContext m_ctx;
    bool m_loading = false;
    QString m_lastPlatform;

    QLabel* m_issues = nullptr;
    QTabWidget* m_tabs = nullptr;

    QLineEdit* m_name = nullptr;
    QLineEdit* m_description = nullptr;
    QComboBox* m_platform = nullptr;
    QComboBox* m_configuration = nullptr;
    QRadioButton* m_buildOnly = nullptr;
    QRadioButton* m_buildPackage = nullptr;
    QCheckBox* m_runAfter = nullptr;
    QLineEdit* m_runArgs = nullptr;
    QLineEdit* m_runDir = nullptr;

    QCheckBox* m_debugTools = nullptr;
    QCheckBox* m_crashReporter = nullptr;
    QLineEdit* m_crashDsn = nullptr;
    QCheckBox* m_engineShared = nullptr;
    QCheckBox* m_customRenderers = nullptr;
    QListWidget* m_renderers = nullptr;
    QToolButton* m_rendererUp = nullptr;
    QToolButton* m_rendererDown = nullptr;
    QCheckBox* m_rendererSelection = nullptr;
    QGroupBox* m_profiling = nullptr;
    QCheckBox* m_profilerOnDemand = nullptr;
    QHash<QString, QCheckBox*> m_profilerCategories;
    QCheckBox* m_sanitize = nullptr;
    QCheckBox* m_warningsAsErrors = nullptr;
    QCheckBox* m_buildTests = nullptr;
    QCheckBox* m_buildSamples = nullptr;

    QWidget* m_packagePage = nullptr;
    QComboBox* m_content = nullptr;
    QComboBox* m_format = nullptr;
    QComboBox* m_symbols = nullptr;
    QCheckBox* m_stripSources = nullptr;
    QCheckBox* m_vcRuntime = nullptr;
    QComboBox* m_keySource = nullptr;
    QLineEdit* m_keyEnv = nullptr;
    QLineEdit* m_key = nullptr;
    QToolButton* m_generateKey = nullptr;
    QLineEdit* m_outputDir = nullptr;
    QLineEdit* m_suffix = nullptr;
    QLineEdit* m_projectName = nullptr;
    QLineEdit* m_version = nullptr;
    QLabel* m_packagePathPreview = nullptr;

    QComboBox* m_generator = nullptr;
    QComboBox* m_architecture = nullptr;
    QLineEdit* m_buildDir = nullptr;
    QLineEdit* m_gameTarget = nullptr;
    QLineEdit* m_extraTargets = nullptr;
    QSpinBox* m_jobs = nullptr;
    QCheckBox* m_cleanFirst = nullptr;
    QCheckBox* m_freshConfigure = nullptr;
    QPlainTextEdit* m_cmakeArgs = nullptr;
    QPlainTextEdit* m_buildToolArgs = nullptr;

    QPlainTextEdit* m_preview = nullptr;
};

}
