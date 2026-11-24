#include "MainWindow.h"

#include "KesStyle.h"
#include "ProfileEditor.h"
#include "Toolchain.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace kbl {

namespace {

const char* kToolName = "Build Launcher";

QString resultKey(const QString& slug)
{
    return "results/" + slug;
}

void drawHammer(QPainter& p, QPointF center, qreal scale)
{
    p.save();
    p.translate(center);
    p.rotate(45);
    p.scale(scale, scale);
    p.drawLine(QPointF(0, -3), QPointF(0, 7));
    p.drawPolygon(QPolygonF({{-5, -7}, {3.5, -7}, {5, -5.5}, {3.5, -4}, {-5, -4}}));
    p.restore();
}

QIcon buildIcon(bool multiple)
{
    QIcon icon;
    for (auto [color, mode] : {std::pair{QColor(0xd2, 0xd2, 0xd7), QIcon::Normal}, std::pair{QColor(0x66, 0x66, 0x6a), QIcon::Disabled}}) {
        QPixmap pm(QSize(32, 32));
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (multiple) {
            drawHammer(p, QPointF(6.0, 7.5), 0.75);
            for (qreal y : {9.5, 12.0, 14.5}) p.drawLine(QPointF(10.5, y), QPointF(14.5, y));
        } else {
            drawHammer(p, QPointF(7.5, 8.5), 0.9);
        }
        p.end();
        icon.addPixmap(pm, mode);
    }
    return icon;
}

}

MainWindow::MainWindow(const QString& engineRoot, QWidget* parent)
    : QMainWindow(parent)
    , m_engineRoot(engineRoot)
{
    m_ctx.engineRoot = engineRoot;
    m_ctx.host = HostInfo::current();
    m_ctx.cmake = findCMake();
    m_store = std::make_unique<ProfileStore>(engineRoot + "/build-profiles");
    m_runner = new BuildRunner(this);

    buildUi();
    setWindowTitle(kes::style::windowTitle(QDir::toNativeSeparators(engineRoot), kToolName));

    if (QDir(m_store->directory()).entryList({"*.json"}, QDir::Files).isEmpty())
        for (const BuildProfile& p : ProfileStore::defaults()) m_store->save(p);
    reloadProfiles(QSettings().value("lastProfile").toString());

    connect(m_runner, &BuildRunner::output, this, &MainWindow::appendLog);
    connect(m_runner, &BuildRunner::stepStarted, this, [this](int index, int count, const Step& step) {
        m_progress->setRange(0, count);
        m_progress->setValue(index);
        if (step.kind == Step::Header) {
            appendLog(QString("=== %1 (%2)").arg(step.title, QDateTime::currentDateTime().toString("HH:mm:ss")), BuildRunner::Stream::Info);
        } else {
            m_stepLabel->setText(step.title);
            appendLog("== " + step.title, BuildRunner::Stream::Info);
        }
    });
    connect(m_runner, &BuildRunner::profileFinished, this, [this](const QString& name, bool ok, qint64 ms) {
        const QString text = QString("=== %1 %2 in %3 s").arg(name, ok ? "succeeded" : "FAILED").arg(ms / 1000.0, 0, 'f', 1);
        appendLog(text, ok ? BuildRunner::Stream::Info : BuildRunner::Stream::Err);
        BuildProfile p;
        p.name = name;
        QSettings().setValue(resultKey(p.slug()), QString("%1|%2").arg(ok ? "ok" : "failed", QDateTime::currentDateTime().toString(Qt::ISODate)));
        refreshList();
    });
    connect(m_runner, &BuildRunner::finished, this, [this](bool ok, bool cancelled) {
        m_progress->setValue(m_progress->maximum());
        const QString text = cancelled ? "Cancelled" : ok ? "Done" : "Failed";
        m_stepLabel->setText(text);
        statusBar()->showMessage(text, 10000);
        QApplication::alert(this);
        updateActions();
    });
}

void MainWindow::buildUi()
{
    auto* toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_newAction = new QAction(kes::style::icon("new"), "New", this);
    m_newAction->setShortcut(QKeySequence::New);
    m_duplicateAction = new QAction(kes::style::icon("copy"), "Duplicate", this);
    m_duplicateAction->setShortcut(QKeySequence("Ctrl+D"));
    m_deleteAction = new QAction(kes::style::icon("trash"), "Delete", this);
    m_buildAction = new QAction(buildIcon(false), "Build", this);
    m_buildAction->setShortcut(QKeySequence("F7"));
    m_buildAction->setToolTip("Build the current profile (F7)");
    m_buildSelectedAction = new QAction(buildIcon(true), "Build Selected", this);
    m_buildSelectedAction->setShortcut(QKeySequence("Ctrl+Shift+B"));
    m_buildSelectedAction->setToolTip("Build every selected profile, one after another (Ctrl+Shift+B)");
    m_cancelAction = new QAction(kes::style::icon("error"), "Cancel", this);
    m_cancelAction->setShortcut(QKeySequence("Ctrl+Break"));
    m_openPackageAction = new QAction(kes::style::icon("open"), "Package Folder", this);
    m_openBuildAction = new QAction(kes::style::icon("open"), "Build Folder", this);

    toolbar->addAction(m_newAction);
    toolbar->addAction(m_duplicateAction);
    toolbar->addAction(m_deleteAction);
    toolbar->addSeparator();
    toolbar->addAction(m_buildAction);
    toolbar->addAction(m_buildSelectedAction);
    toolbar->addAction(m_cancelAction);
    toolbar->addSeparator();
    toolbar->addAction(m_openPackageAction);
    toolbar->addAction(m_openBuildAction);

    QMenu* file = menuBar()->addMenu("&Profile");
    file->addAction(m_newAction);
    file->addAction(m_duplicateAction);
    file->addAction(m_deleteAction);
    file->addSeparator();
    file->addAction("Reload Profiles", this, [this] {
        reloadProfiles(m_current >= 0 ? m_profiles[m_current].name : QString());
    });
    file->addAction("Open Profiles Folder", this, [this] { openFolder(m_store->directory()); });
    file->addSeparator();
    file->addAction("Quit", QKeySequence::Quit, this, &QWidget::close);
    QMenu* build = menuBar()->addMenu("&Build");
    build->addAction(m_buildAction);
    build->addAction(m_buildSelectedAction);
    build->addAction(m_cancelAction);
    build->addSeparator();
    build->addAction(m_openPackageAction);
    build->addAction(m_openBuildAction);
    build->addAction("Clear Log", this, [this] { m_log->clear(); });

    connect(m_newAction, &QAction::triggered, this, &MainWindow::newProfile);
    connect(m_duplicateAction, &QAction::triggered, this, &MainWindow::duplicateProfile);
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteProfile);
    connect(m_buildAction, &QAction::triggered, this, [this] {
        if (m_current >= 0) buildProfiles({m_current});
    });
    connect(m_buildSelectedAction, &QAction::triggered, this, [this] { buildProfiles(selectedRows()); });
    connect(m_cancelAction, &QAction::triggered, m_runner, &BuildRunner::cancel);
    connect(m_openPackageAction, &QAction::triggered, this, [this] {
        if (m_current >= 0) openFolder(packageDirectory(m_profiles[m_current], m_engineRoot));
    });
    connect(m_openBuildAction, &QAction::triggered, this, [this] {
        if (m_current >= 0) openFolder(buildDirectory(m_profiles[m_current], m_engineRoot));
    });

    auto* vertical = new QSplitter(Qt::Vertical);
    auto* horizontal = new QSplitter(Qt::Horizontal);

    auto* listPanel = new QWidget;
    auto* listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(4, 4, 0, 4);
    listLayout->addWidget(new QLabel("Profiles"));
    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listLayout->addWidget(m_list, 1);
    horizontal->addWidget(listPanel);

    m_editor = new ProfileEditor;
    m_editor->setContext(m_ctx);
    horizontal->addWidget(m_editor);
    horizontal->setStretchFactor(0, 0);
    horizontal->setStretchFactor(1, 1);
    horizontal->setSizes({220, 700});
    vertical->addWidget(horizontal);

    auto* logPanel = new QWidget;
    auto* logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(4, 0, 4, 4);
    auto* statusRow = new QHBoxLayout;
    m_stepLabel = new QLabel("Ready");
    m_progress = new QProgressBar;
    m_progress->setMaximumWidth(220);
    m_progress->setTextVisible(false);
    m_progress->setValue(0);
    statusRow->addWidget(m_stepLabel, 1);
    statusRow->addWidget(m_progress);
    logLayout->addLayout(statusRow);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(50000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    logLayout->addWidget(m_log, 1);
    vertical->addWidget(logPanel);
    vertical->setSizes({520, 260});
    setCentralWidget(vertical);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveCurrent);

    connect(m_list, &QListWidget::currentRowChanged, this, &MainWindow::selectProfile);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &MainWindow::updateActions);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] {
        if (m_current >= 0) buildProfiles({m_current});
    });
    connect(m_editor, &ProfileEditor::edited, this, &MainWindow::profileEdited);

    resize(1100, 820);
    const QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_runner->isRunning() &&
        QMessageBox::question(this, kToolName, "A build is running. Cancel it and quit?") != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    m_runner->cancel();
    if (m_saveTimer->isActive()) saveCurrent();
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    if (m_current >= 0) settings.setValue("lastProfile", m_profiles[m_current].name);
    event->accept();
}

void MainWindow::reloadProfiles(const QString& select)
{
    QStringList errors;
    m_profiles = m_store->load(&errors);
    for (const QString& e : errors) appendLog("Couldn't load profile " + e, BuildRunner::Stream::Err);
    m_current = -1;
    refreshList();
    int row = 0;
    for (int i = 0; i < m_profiles.size(); ++i)
        if (m_profiles[i].name == select) row = i;
    if (m_profiles.isEmpty()) {
        selectProfile(-1);
    } else {
        m_list->setCurrentRow(row);
        selectProfile(row);
    }
}

void MainWindow::refreshList()
{
    const QSignalBlocker block(m_list);
    const int current = m_list->currentRow();
    QList<int> selected = selectedRows();
    m_list->clear();
    const QSettings settings;
    for (const BuildProfile& p : m_profiles) {
        auto* item = new QListWidgetItem(p.name, m_list);
        QString tip = p.description;
        const QStringList result = settings.value(resultKey(p.slug())).toString().split('|');
        if (result.size() == 2) {
            const bool ok = result[0] == QLatin1String("ok");
            item->setIcon(kes::style::icon(ok ? "info" : "error"));
            const QDateTime when = QDateTime::fromString(result[1], Qt::ISODate);
            tip += QString("\nLast build %1 at %2").arg(ok ? "succeeded" : "failed", when.toString("yyyy-MM-dd HH:mm"));
        }
        tip += QString("\n%1, %2").arg(configurationLabel(p.configuration), p.package.enabled ? "packaged" : "build only");
        item->setToolTip(tip.trimmed());
    }
    if (current >= 0 && current < m_list->count()) m_list->setCurrentRow(current);
    for (int row : selected)
        if (row < m_list->count()) m_list->item(row)->setSelected(true);
}

QList<int> MainWindow::selectedRows() const
{
    QList<int> rows;
    for (const QListWidgetItem* item : m_list->selectedItems()) rows << m_list->row(item);
    std::sort(rows.begin(), rows.end());
    return rows;
}

void MainWindow::selectProfile(int row)
{
    if (m_saveTimer->isActive()) saveCurrent();
    m_current = row >= 0 && row < m_profiles.size() ? row : -1;
    m_editor->setEnabled(m_current >= 0);
    if (m_current >= 0) {
        m_savedName = m_profiles[m_current].name;
        m_loadingProfile = true;
        m_editor->setProfile(m_profiles[m_current]);
        m_loadingProfile = false;
    }
    updateActions();
}

void MainWindow::profileEdited()
{
    if (m_loadingProfile || m_current < 0) return;
    m_profiles[m_current] = m_editor->profile();
    if (QListWidgetItem* item = m_list->item(m_current)) item->setText(m_profiles[m_current].name);
    m_saveTimer->start();
}

void MainWindow::saveCurrent()
{
    m_saveTimer->stop();
    if (m_current < 0) return;
    const BuildProfile& p = m_profiles[m_current];
    if (p.name.trimmed().isEmpty()) return;
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (i != m_current && m_profiles[i].slug() == p.slug()) {
            statusBar()->showMessage(QString("Not saved: '%1' is already used by another profile.").arg(p.name), 8000);
            return;
        }
    }
    QString error;
    if (!m_store->rename(m_savedName, p, &error)) {
        statusBar()->showMessage("Couldn't save the profile: " + error, 8000);
        return;
    }
    m_savedName = p.name;
    statusBar()->showMessage("Saved " + QDir::toNativeSeparators(m_store->filePath(p)), 3000);
}

void MainWindow::newProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Profile", "Name:", QLineEdit::Normal,
                                               m_store->uniqueName("New Profile", m_profiles), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    BuildProfile p;
    p.name = m_store->uniqueName(name, m_profiles);
    m_store->save(p);
    reloadProfiles(p.name);
}

void MainWindow::duplicateProfile()
{
    if (m_current < 0) return;
    saveCurrent();
    BuildProfile p = m_profiles[m_current];
    p.name = m_store->uniqueName(p.name + " Copy", m_profiles);
    if (!p.package.suffix.isEmpty()) p.package.suffix += "-copy";
    m_store->save(p);
    reloadProfiles(p.name);
}

void MainWindow::deleteProfile()
{
    if (m_current < 0) return;
    const BuildProfile p = m_profiles[m_current];
    if (QMessageBox::question(this, kToolName, QString("Delete the profile '%1'?\n\nIts build folder is kept:\n%2")
                                                   .arg(p.name, QDir::toNativeSeparators(buildDirectory(p, m_engineRoot)))) != QMessageBox::Yes)
        return;
    m_saveTimer->stop();
    m_store->remove(p);
    reloadProfiles();
}

void MainWindow::buildProfiles(const QList<int>& rows)
{
    if (m_runner->isRunning() || rows.isEmpty()) return;
    saveCurrent();
    QList<Step> steps;
    QStringList problems;
    for (int row : rows) {
        BuildProfile p = m_profiles[row];
        if (row != rows.last()) p.launch.runAfterBuild = false;
        const Plan plan = makePlan(p, m_ctx);
        for (const Issue& issue : plan.issues)
            if (issue.severity == Issue::Error) problems << QString("%1: %2").arg(p.name, issue.text);
        steps << plan.steps;
    }
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, kToolName, "Can't build:\n\n" + problems.join('\n'));
        return;
    }
    m_log->clear();
    appendLog("Engine: " + QDir::toNativeSeparators(m_engineRoot) + "    CMake: " + QDir::toNativeSeparators(m_ctx.cmake),
              BuildRunner::Stream::Info);
    m_runner->start(steps);
    updateActions();
}

void MainWindow::appendLog(const QString& line, BuildRunner::Stream stream)
{
    QColor color = kes::style::text();
    if (stream == BuildRunner::Stream::Info) {
        color = kes::style::accent();
    } else if (stream == BuildRunner::Stream::Err || line.contains(QLatin1String(" error "), Qt::CaseInsensitive) ||
               line.contains(QLatin1String("error:"), Qt::CaseInsensitive) || line.contains(QLatin1String("FAILED"))) {
        color = kes::style::error();
    } else if (line.contains(QLatin1String("warning"), Qt::CaseInsensitive)) {
        color = kes::style::warning();
    } else if (line.startsWith(QLatin1String("-- Package:")) || line.contains(QLatin1String("-- Package:"))) {
        color = kes::style::info();
    }
    m_log->appendHtml(QString("<span style='color:%1; white-space:pre'>%2</span>").arg(color.name(), line.toHtmlEscaped()));
}

void MainWindow::updateActions()
{
    const bool running = m_runner->isRunning();
    const bool has = m_current >= 0;
    m_buildAction->setEnabled(!running && has);
    m_buildSelectedAction->setEnabled(!running && m_list->selectedItems().size() > 0);
    m_cancelAction->setEnabled(running);
    m_duplicateAction->setEnabled(has);
    m_deleteAction->setEnabled(has && !running);
    m_openPackageAction->setEnabled(has);
    m_openBuildAction->setEnabled(has);
}

void MainWindow::openFolder(const QString& path)
{
    if (!QDir(path).exists()) {
        statusBar()->showMessage("Doesn't exist yet: " + QDir::toNativeSeparators(path), 5000);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

}
