#include "ProfileEditor.h"

#include "KesStyle.h"
#include "Toolchain.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace kbl {

namespace {

QStringList splitLines(const QString& text)
{
    QStringList out;
    for (const QString& line : text.split('\n')) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) out << t;
    }
    return out;
}

QStringList splitWords(const QString& text)
{
    return text.split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
}

void setComboData(QComboBox* combo, const QString& value)
{
    const int index = combo->findData(value);
    if (index >= 0) {
        combo->setCurrentIndex(index);
    } else if (combo->isEditable()) {
        combo->setEditText(value);
    } else {
        combo->setCurrentIndex(0);
    }
}

QString comboValue(const QComboBox* combo)
{
    if (combo->isEditable()) {
        const int index = combo->findText(combo->currentText());
        return index >= 0 ? combo->itemData(index).toString() : combo->currentText().trimmed();
    }
    return combo->currentData().toString();
}

QWidget* scrollable(QWidget* content)
{
    auto* scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QLabel* hint(const QString& text)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, kes::style::muted());
    label->setPalette(p);
    return label;
}

QWidget* withBrowse(QLineEdit* edit, const QString& caption, const std::function<QString()>& base)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit, 1);
    auto* browse = new QToolButton;
    browse->setText("...");
    QObject::connect(browse, &QToolButton::clicked, edit, [edit, caption, base] {
        const QString start = edit->text().isEmpty() ? base() : QDir(base()).absoluteFilePath(edit->text());
        const QString dir = QFileDialog::getExistingDirectory(edit, caption, start);
        if (dir.isEmpty()) return;
        const QString relative = QDir(base()).relativeFilePath(dir);
        edit->setText(relative.startsWith("..") ? QDir::fromNativeSeparators(dir) : relative);
    });
    layout->addWidget(browse);
    return row;
}

}

ProfileEditor::ProfileEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_issues = new QLabel;
    m_issues->setWordWrap(true);
    m_issues->setTextFormat(Qt::RichText);
    m_issues->setContentsMargins(8, 6, 8, 6);
    m_issues->setAutoFillBackground(true);
    layout->addWidget(m_issues);

    m_tabs = new QTabWidget;
    m_tabs->addTab(makeGeneralTab(), "General");
    m_tabs->addTab(makeEngineTab(), "Engine");
    m_tabs->addTab(makePackageTab(), "Packaging");
    m_tabs->addTab(makeAdvancedTab(), "Advanced");
    m_tabs->addTab(makePreviewTab(), "Commands");
    layout->addWidget(m_tabs, 1);
}

void ProfileEditor::wire(QObject* w)
{
    if (auto* e = qobject_cast<QLineEdit*>(w)) connect(e, &QLineEdit::textEdited, this, &ProfileEditor::changed);
    else if (auto* c = qobject_cast<QComboBox*>(w)) {
        connect(c, &QComboBox::currentIndexChanged, this, &ProfileEditor::changed);
        if (c->isEditable()) connect(c, &QComboBox::editTextChanged, this, &ProfileEditor::changed);
    }
    else if (auto* b = qobject_cast<QCheckBox*>(w)) connect(b, &QCheckBox::toggled, this, &ProfileEditor::changed);
    else if (auto* r = qobject_cast<QRadioButton*>(w)) connect(r, &QRadioButton::toggled, this, &ProfileEditor::changed);
    else if (auto* g = qobject_cast<QGroupBox*>(w)) connect(g, &QGroupBox::toggled, this, &ProfileEditor::changed);
    else if (auto* s = qobject_cast<QSpinBox*>(w)) connect(s, &QSpinBox::valueChanged, this, &ProfileEditor::changed);
    else if (auto* t = qobject_cast<QPlainTextEdit*>(w)) connect(t, &QPlainTextEdit::textChanged, this, &ProfileEditor::changed);
}

QWidget* ProfileEditor::makeGeneralTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* profileBox = new QGroupBox("Profile");
    auto* form = new QFormLayout(profileBox);
    m_name = new QLineEdit;
    m_description = new QLineEdit;
    form->addRow("Name", m_name);
    form->addRow("Description", m_description);
    layout->addWidget(profileBox);

    auto* targetBox = new QGroupBox("Target");
    form = new QFormLayout(targetBox);
    m_platform = new QComboBox;
    form->addRow("Platform", m_platform);
    m_configuration = new QComboBox;
    for (const QString& c : configurations()) m_configuration->addItem(configurationLabel(c), c);
    form->addRow("Configuration", m_configuration);
    form->addRow(hint("Debug: no optimization, full checks. Development: optimized with debug info. "
                      "Release: optimized; still writes symbols for crash reports. MinSizeRel: optimized for size."));
    layout->addWidget(targetBox);

    auto* outputBox = new QGroupBox("Output");
    auto* outputLayout = new QVBoxLayout(outputBox);
    m_buildOnly = new QRadioButton("Build only: the game next to its build folder, loose content");
    m_buildPackage = new QRadioButton("Build and package: a standalone folder/archive (see Packaging)");
    outputLayout->addWidget(m_buildOnly);
    outputLayout->addWidget(m_buildPackage);
    layout->addWidget(outputBox);

    auto* launchBox = new QGroupBox("After building");
    form = new QFormLayout(launchBox);
    m_runAfter = new QCheckBox("Run the game");
    m_runArgs = new QLineEdit;
    m_runArgs->setPlaceholderText("e.g. -renderer=d3d11 -map=test");
    m_runDir = new QLineEdit;
    m_runDir->setPlaceholderText("the executable's folder");
    form->addRow(m_runAfter);
    form->addRow("Arguments", m_runArgs);
    form->addRow("Working folder", withBrowse(m_runDir, "Working folder", [this] { return m_ctx.engineRoot; }));
    layout->addWidget(launchBox);
    layout->addStretch();

    for (QObject* w : std::initializer_list<QObject*>{m_name, m_description, m_platform, m_configuration, m_buildOnly,
                                                       m_runAfter, m_runArgs, m_runDir})
        wire(w);
    return scrollable(page);
}

QWidget* ProfileEditor::makeEngineTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* featureBox = new QGroupBox("Features");
    auto* form = new QFormLayout(featureBox);
    m_debugTools = new QCheckBox("Debug tools (F1-F4 toggles, F7 UI reload, F8 UI inspector)");
    m_crashReporter = new QCheckBox("Crash reporter (Crashpad minidumps)");
    m_crashDsn = new QLineEdit;
    m_crashDsn->setPlaceholderText("empty: keep minidumps on the player's machine");
    m_engineShared = new QCheckBox("Engine as a shared library (engine.dll / libengine.so)");
    form->addRow(m_debugTools);
    form->addRow(m_crashReporter);
    form->addRow("Crash upload DSN", m_crashDsn);
    form->addRow(m_engineShared);
    layout->addWidget(featureBox);

    auto* rendererBox = new QGroupBox("Renderers");
    auto* rendererLayout = new QVBoxLayout(rendererBox);
    m_customRenderers = new QCheckBox("Choose renderers (otherwise the engine's default for the platform)");
    rendererLayout->addWidget(m_customRenderers);
    auto* listRow = new QHBoxLayout;
    m_renderers = new QListWidget;
    m_renderers->setMaximumHeight(80);
    listRow->addWidget(m_renderers, 1);
    auto* buttons = new QVBoxLayout;
    m_rendererUp = new QToolButton;
    m_rendererUp->setIcon(kes::style::icon("up"));
    m_rendererUp->setToolTip("Higher priority");
    m_rendererDown = new QToolButton;
    m_rendererDown->setIcon(kes::style::icon("down"));
    m_rendererDown->setToolTip("Lower priority");
    buttons->addWidget(m_rendererUp);
    buttons->addWidget(m_rendererDown);
    buttons->addStretch();
    listRow->addLayout(buttons);
    rendererLayout->addLayout(listRow);
    rendererLayout->addWidget(hint("Checked renderers are compiled in, the first one is preferred. Only one checked forces it."));
    m_rendererSelection = new QCheckBox("Allow choosing the renderer at runtime (-renderer=, KES_RENDERER)");
    rendererLayout->addWidget(m_rendererSelection);
    layout->addWidget(rendererBox);

    auto move = [this](int delta) {
        const int row = m_renderers->currentRow();
        const int target = row + delta;
        if (row < 0 || target < 0 || target >= m_renderers->count()) return;
        QListWidgetItem* item = m_renderers->takeItem(row);
        m_renderers->insertItem(target, item);
        m_renderers->setCurrentRow(target);
        changed();
    };
    connect(m_rendererUp, &QToolButton::clicked, this, [move] { move(-1); });
    connect(m_rendererDown, &QToolButton::clicked, this, [move] { move(1); });
    connect(m_renderers, &QListWidget::itemChanged, this, &ProfileEditor::changed);

    m_profiling = new QGroupBox("Tracy profiling");
    m_profiling->setCheckable(true);
    auto* profilingLayout = new QVBoxLayout(m_profiling);
    m_profilerOnDemand = new QCheckBox("Only collect while a viewer is connected (on demand)");
    profilingLayout->addWidget(m_profilerOnDemand);
    auto* grid = new QGridLayout;
    int i = 0;
    for (const QString& category : profilerCategories()) {
        auto* box = new QCheckBox(category);
        m_profilerCategories.insert(category, box);
        grid->addWidget(box, i / 4, i % 4);
        wire(box);
        ++i;
    }
    profilingLayout->addWidget(hint("Zone categories:"));
    profilingLayout->addLayout(grid);
    layout->addWidget(m_profiling);

    auto* checksBox = new QGroupBox("Checks and extras");
    form = new QFormLayout(checksBox);
    m_sanitize = new QCheckBox("Address + undefined behavior sanitizers (ASan only on MSVC)");
    m_warningsAsErrors = new QCheckBox("Treat warnings as errors");
    m_buildTests = new QCheckBox("Configure the unit tests (tests/)");
    m_buildSamples = new QCheckBox("Configure the samples (samples/)");
    form->addRow(m_sanitize);
    form->addRow(m_warningsAsErrors);
    form->addRow(m_buildTests);
    form->addRow(m_buildSamples);
    form->addRow(hint("Tests and samples are only built when listed under Advanced > Extra targets."));
    layout->addWidget(checksBox);
    layout->addStretch();

    for (QObject* w : std::initializer_list<QObject*>{m_debugTools, m_crashReporter, m_crashDsn, m_engineShared,
                                                       m_customRenderers, m_rendererSelection, m_profiling,
                                                       m_profilerOnDemand, m_sanitize, m_warningsAsErrors,
                                                       m_buildTests, m_buildSamples})
        wire(w);
    return scrollable(page);
}

QWidget* ProfileEditor::makePackageTab()
{
    auto* page = new QWidget;
    m_packagePage = page;
    auto* layout = new QVBoxLayout(page);

    auto* contentBox = new QGroupBox("Content");
    auto* form = new QFormLayout(contentBox);
    m_content = new QComboBox;
    m_content->addItem("Pak files (content/game.pak, content/engine.pak)", "pak");
    m_content->addItem("Loose files (content/..., like a development build)", "loose");
    form->addRow("Content", m_content);
    m_stripSources = new QCheckBox("Leave out sources (.slang, .slangnodes, .fmap)");
    form->addRow(m_stripSources);

    m_keySource = new QComboBox;
    m_keySource->addItem("No encryption", "none");
    m_keySource->addItem("Key from an environment variable", "env");
    m_keySource->addItem("Key stored in this profile", "inline");
    form->addRow("Pak encryption", m_keySource);
    m_keyEnv = new QLineEdit;
    form->addRow("Variable", m_keyEnv);
    auto* keyRow = new QWidget;
    auto* keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    m_key = new QLineEdit;
    m_key->setPlaceholderText("64 hex characters");
    m_key->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_generateKey = new QToolButton;
    m_generateKey->setText("Generate");
    keyLayout->addWidget(m_key, 1);
    keyLayout->addWidget(m_generateKey);
    form->addRow("Key", keyRow);
    connect(m_generateKey, &QToolButton::clicked, this, [this] {
        QString key;
        for (int i = 0; i < 32; ++i)
            key += QString("%1").arg(QRandomGenerator::system()->bounded(256), 2, 16, QChar('0'));
        m_key->setText(key);
        changed();
    });
    layout->addWidget(contentBox);

    auto* outputBox = new QGroupBox("Package");
    form = new QFormLayout(outputBox);
    m_format = new QComboBox;
    m_format->addItem("Platform default", "");
    m_format->addItem("zip archive", "zip");
    m_format->addItem("Folder only", "folder");
    m_format->addItem("tar.gz archive", "tar.gz");
    m_format->addItem("MSIXVC (GDK, needs makepkg)", "msixvc");
    form->addRow("Format", m_format);
    m_symbols = new QComboBox;
    m_symbols->addItem("Separate symbols archive, not shipped", "archive");
    m_symbols->addItem("Next to the binaries (PDB / debug info in the package)", "package");
    m_symbols->addItem("Both", "both");
    m_symbols->addItem("None (strip, keep nothing)", "none");
    form->addRow("Debug symbols", m_symbols);
    m_vcRuntime = new QCheckBox("Bundle the MSVC runtime DLLs (Windows)");
    form->addRow(m_vcRuntime);
    m_outputDir = new QLineEdit;
    m_outputDir->setPlaceholderText("out/package");
    form->addRow("Output folder", withBrowse(m_outputDir, "Package output folder", [this] { return m_ctx.engineRoot; }));
    m_suffix = new QLineEdit;
    m_suffix->setPlaceholderText("e.g. -debug-pak");
    form->addRow("Name suffix", m_suffix);
    m_projectName = new QLineEdit;
    m_projectName->setPlaceholderText("From the .kproject");
    form->addRow("Project name", m_projectName);
    m_version = new QLineEdit;
    m_version->setPlaceholderText("From the .kproject, e.g. 1.0.0.0");
    form->addRow("Version", m_version);
    m_packagePathPreview = hint({});
    m_packagePathPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow("Writes", m_packagePathPreview);
    layout->addWidget(outputBox);
    layout->addStretch();

    for (QObject* w : std::initializer_list<QObject*>{m_content, m_stripSources, m_keySource, m_keyEnv, m_key, m_format,
                                                       m_symbols, m_vcRuntime, m_outputDir, m_suffix, m_projectName, m_version})
        wire(w);
    return scrollable(page);
}

QWidget* ProfileEditor::makeAdvancedTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* cmakeBox = new QGroupBox("CMake");
    auto* form = new QFormLayout(cmakeBox);
    m_generator = new QComboBox;
    m_generator->setEditable(true);
    for (const QString& g : knownGenerators()) m_generator->addItem(g.isEmpty() ? "CMake default" : g, g);
    form->addRow("Generator", m_generator);
    m_architecture = new QComboBox;
    for (const QString& a : {"x64", "ARM64", "Win32"}) m_architecture->addItem(a, a);
    form->addRow("Architecture (Visual Studio)", m_architecture);
    m_buildDir = new QLineEdit;
    form->addRow("Build folder", withBrowse(m_buildDir, "Build folder", [this] { return m_ctx.engineRoot; }));
    m_freshConfigure = new QCheckBox("Fresh configure (cmake --fresh: rebuild the cache from scratch)");
    form->addRow(m_freshConfigure);
    m_cmakeArgs = new QPlainTextEdit;
    m_cmakeArgs->setPlaceholderText("One per line, e.g.\n-DKES_GDK_TITLE_ID=12345678");
    m_cmakeArgs->setMaximumHeight(90);
    form->addRow("Extra configure arguments", m_cmakeArgs);
    layout->addWidget(cmakeBox);

    auto* buildBox = new QGroupBox("Build");
    form = new QFormLayout(buildBox);
    m_gameTarget = new QLineEdit;
    form->addRow("Game target", m_gameTarget);
    m_extraTargets = new QLineEdit;
    m_extraTargets->setPlaceholderText("e.g. engine_tests forge-map-compiler");
    form->addRow("Extra targets", m_extraTargets);
    m_jobs = new QSpinBox;
    m_jobs->setRange(0, 256);
    m_jobs->setSpecialValueText("Default");
    form->addRow("Parallel jobs", m_jobs);
    m_cleanFirst = new QCheckBox("Clean before building (--clean-first)");
    form->addRow(m_cleanFirst);
    m_buildToolArgs = new QPlainTextEdit;
    m_buildToolArgs->setPlaceholderText("One per line, passed after \"--\" to MSBuild / Ninja, e.g.\n/v:minimal");
    m_buildToolArgs->setMaximumHeight(70);
    form->addRow("Build tool arguments", m_buildToolArgs);
    layout->addWidget(buildBox);
    layout->addStretch();

    for (QObject* w : std::initializer_list<QObject*>{m_generator, m_architecture, m_buildDir, m_freshConfigure, m_cmakeArgs,
                                                       m_gameTarget, m_extraTargets, m_jobs, m_cleanFirst, m_buildToolArgs})
        wire(w);
    return scrollable(page);
}

QWidget* ProfileEditor::makePreviewTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(hint("What this profile runs. The same build works from a terminal: "
                           "kesbuild \"<profile>\" (tools/bin), or the commands below."));
    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_preview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    layout->addWidget(m_preview, 1);
    return page;
}

void ProfileEditor::setContext(const PlanContext& ctx)
{
    m_ctx = ctx;
    const bool loading = m_loading;
    m_loading = true;
    m_platform->clear();
    m_platform->addItem(QString("This machine (%1)").arg(ctx.host.defaultPlatform), "Host");
    for (const QString& p : ctx.host.platforms) m_platform->addItem(p, p);
    m_architecture->setEnabled(ctx.host.windows);
    m_loading = loading;
}

QString ProfileEditor::currentPlatform() const
{
    BuildProfile p;
    p.platform = m_platform->currentData().toString();
    return resolvePlatform(p, m_ctx.host);
}

void ProfileEditor::fillRenderers(const QString& platform, const QStringList& order, const QStringList& enabled)
{
    const QSignalBlocker block(m_renderers);
    m_renderers->clear();
    QStringList all = order;
    for (const QString& r : availableRenderers(platform))
        if (!all.contains(r)) all << r;
    for (const QString& r : all) {
        auto* item = new QListWidgetItem(r, m_renderers);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(enabled.contains(r) ? Qt::Checked : Qt::Unchecked);
    }
}

void ProfileEditor::setProfile(const BuildProfile& p)
{
    m_loading = true;
    m_name->setText(p.name);
    m_description->setText(p.description);
    setComboData(m_platform, p.platform);
    setComboData(m_configuration, p.configuration);
    m_buildOnly->setChecked(!p.package.enabled);
    m_buildPackage->setChecked(p.package.enabled);
    m_runAfter->setChecked(p.launch.runAfterBuild);
    m_runArgs->setText(p.launch.arguments);
    m_runDir->setText(p.launch.workingDir);

    m_debugTools->setChecked(p.engine.debugTools);
    m_crashReporter->setChecked(p.engine.crashReporter);
    m_crashDsn->setText(p.engine.crashDsn);
    m_engineShared->setChecked(p.engine.engineShared);
    const QString platform = resolvePlatform(p, m_ctx.host);
    m_lastPlatform = platform;
    m_customRenderers->setChecked(!p.engine.renderers.isEmpty());
    const QStringList renderers = p.engine.renderers.isEmpty() ? defaultRenderers(platform) : p.engine.renderers;
    fillRenderers(platform, renderers, renderers);
    m_rendererSelection->setChecked(p.engine.rendererSelection);
    m_profiling->setChecked(p.engine.profiling);
    m_profilerOnDemand->setChecked(p.engine.profilerOnDemand);
    for (auto it = m_profilerCategories.begin(); it != m_profilerCategories.end(); ++it)
        it.value()->setChecked(!p.engine.disabledProfilerCategories.contains(it.key()));
    m_sanitize->setChecked(p.engine.sanitize);
    m_warningsAsErrors->setChecked(p.engine.warningsAsErrors);
    m_buildTests->setChecked(p.engine.buildTests);
    m_buildSamples->setChecked(p.engine.buildSamples);

    setComboData(m_content, p.package.content);
    setComboData(m_format, p.package.format);
    setComboData(m_symbols, p.package.symbols);
    m_stripSources->setChecked(p.package.stripSources);
    m_vcRuntime->setChecked(p.package.vcRuntime);
    setComboData(m_keySource, p.package.keySource);
    m_keyEnv->setText(p.package.keyEnv);
    m_key->setText(p.package.key);
    m_outputDir->setText(p.package.outputDir);
    m_suffix->setText(p.package.suffix);
    m_projectName->setText(p.package.projectName);
    m_version->setText(p.package.version);

    setComboData(m_generator, p.advanced.generator);
    setComboData(m_architecture, p.advanced.architecture);
    m_buildDir->setText(p.advanced.buildDir);
    m_buildDir->setPlaceholderText("out/build/profile-" + p.slug());
    m_gameTarget->setText(p.advanced.gameTarget);
    m_extraTargets->setText(p.advanced.extraTargets.join(' '));
    m_jobs->setValue(p.advanced.jobs);
    m_cleanFirst->setChecked(p.advanced.cleanFirst);
    m_freshConfigure->setChecked(p.advanced.freshConfigure);
    m_cmakeArgs->setPlainText(p.advanced.cmakeArgs.join('\n'));
    m_buildToolArgs->setPlainText(p.advanced.buildToolArgs.join('\n'));
    m_loading = false;
    refresh();
}

BuildProfile ProfileEditor::profile() const
{
    BuildProfile p;
    p.name = m_name->text().trimmed();
    p.description = m_description->text().trimmed();
    p.platform = m_platform->currentData().toString();
    p.configuration = m_configuration->currentData().toString();
    p.package.enabled = m_buildPackage->isChecked();
    p.launch.runAfterBuild = m_runAfter->isChecked();
    p.launch.arguments = m_runArgs->text().trimmed();
    p.launch.workingDir = m_runDir->text().trimmed();

    p.engine.debugTools = m_debugTools->isChecked();
    p.engine.crashReporter = m_crashReporter->isChecked();
    p.engine.crashDsn = m_crashDsn->text().trimmed();
    p.engine.engineShared = m_engineShared->isChecked();
    if (m_customRenderers->isChecked()) {
        for (int i = 0; i < m_renderers->count(); ++i)
            if (m_renderers->item(i)->checkState() == Qt::Checked) p.engine.renderers << m_renderers->item(i)->text();
    }
    p.engine.rendererSelection = m_rendererSelection->isChecked();
    p.engine.profiling = m_profiling->isChecked();
    p.engine.profilerOnDemand = m_profilerOnDemand->isChecked();
    for (const QString& category : profilerCategories())
        if (!m_profilerCategories.value(category)->isChecked()) p.engine.disabledProfilerCategories << category;
    p.engine.sanitize = m_sanitize->isChecked();
    p.engine.warningsAsErrors = m_warningsAsErrors->isChecked();
    p.engine.buildTests = m_buildTests->isChecked();
    p.engine.buildSamples = m_buildSamples->isChecked();

    p.package.content = m_content->currentData().toString();
    p.package.format = m_format->currentData().toString();
    p.package.symbols = m_symbols->currentData().toString();
    p.package.stripSources = m_stripSources->isChecked();
    p.package.vcRuntime = m_vcRuntime->isChecked();
    p.package.keySource = m_keySource->currentData().toString();
    p.package.keyEnv = m_keyEnv->text().trimmed();
    p.package.key = m_key->text().trimmed();
    p.package.outputDir = m_outputDir->text().trimmed();
    p.package.suffix = m_suffix->text().trimmed();
    p.package.projectName = m_projectName->text().trimmed();
    p.package.version = m_version->text().trimmed();

    p.advanced.generator = comboValue(m_generator);
    p.advanced.architecture = m_architecture->currentData().toString();
    p.advanced.buildDir = m_buildDir->text().trimmed();
    p.advanced.gameTarget = m_gameTarget->text().trimmed();
    p.advanced.extraTargets = splitWords(m_extraTargets->text());
    p.advanced.jobs = m_jobs->value();
    p.advanced.cleanFirst = m_cleanFirst->isChecked();
    p.advanced.freshConfigure = m_freshConfigure->isChecked();
    p.advanced.cmakeArgs = splitLines(m_cmakeArgs->toPlainText());
    p.advanced.buildToolArgs = splitLines(m_buildToolArgs->toPlainText());
    return p;
}

void ProfileEditor::changed()
{
    if (m_loading) return;
    const QString platform = currentPlatform();
    if (platform != m_lastPlatform) {
        m_lastPlatform = platform;
        const QStringList defaults = defaultRenderers(platform);
        fillRenderers(platform, defaults, defaults);
    }
    refresh();
    emit edited();
}

void ProfileEditor::refresh()
{
    const BuildProfile p = profile();

    m_crashDsn->setEnabled(p.engine.crashReporter);
    m_renderers->setEnabled(m_customRenderers->isChecked());
    m_rendererUp->setEnabled(m_customRenderers->isChecked());
    m_rendererDown->setEnabled(m_customRenderers->isChecked());
    m_keyEnv->setEnabled(p.package.keySource == QLatin1String("env"));
    m_key->setEnabled(p.package.keySource == QLatin1String("inline"));
    m_generateKey->setEnabled(p.package.keySource == QLatin1String("inline"));
    m_vcRuntime->setEnabled(m_ctx.host.windows);
    m_packagePage->setEnabled(p.package.enabled);
    m_runArgs->setEnabled(p.launch.runAfterBuild);
    m_runDir->parentWidget()->setEnabled(p.launch.runAfterBuild);
    m_buildDir->setPlaceholderText("out/build/profile-" + p.slug());

    const QString platform = resolvePlatform(p, m_ctx.host);
    const QString format = p.package.format.isEmpty() ? defaultPackageFormat(platform) : p.package.format;
    const QString name = p.package.projectName.isEmpty() ? QStringLiteral("<project>") : p.package.projectName;
    const QString folder = QDir::toNativeSeparators(packageDirectory(p, m_ctx.engineRoot) + '/' + name + '-' + platform.toLower() + p.package.suffix);
    QString writes = folder;
    if (format == QLatin1String("zip") || format == QLatin1String("tar.gz"))
        writes += QString("\n%1-<version>.%2").arg(folder, format);
    else if (format == QLatin1String("msixvc"))
        writes += QString("\n%1-msixvc").arg(folder);
    if (p.package.symbols == QLatin1String("archive") || p.package.symbols == QLatin1String("both"))
        writes += QString("\n%1-symbols-<version>.zip").arg(folder);
    m_packagePathPreview->setText(writes);

    const Plan plan = makePlan(p, m_ctx);
    QStringList lines;
    for (const Issue& issue : plan.issues) {
        const QColor color = issue.severity == Issue::Error ? kes::style::error()
                           : issue.severity == Issue::Warning ? kes::style::warning() : kes::style::info();
        const char* tag = issue.severity == Issue::Error ? "Error" : issue.severity == Issue::Warning ? "Warning" : "Note";
        lines << QString("<span style='color:%1'><b>%2:</b></span> %3").arg(color.name(), tag, issue.text.toHtmlEscaped());
    }
    m_issues->setText(lines.join("<br>"));
    m_issues->setVisible(!lines.isEmpty());

    QStringList commands;
    for (const Step& s : plan.steps)
        if (s.kind != Step::Header) commands << commandLine(s);
    m_preview->setPlainText(plan.ok() ? commands.join("\n\n") : QStringLiteral("Fix the errors above first."));
}

}
