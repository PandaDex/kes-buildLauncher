#include "BuildRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace kbl {

BuildRunner::BuildRunner(QObject* parent)
    : QObject(parent)
{
}

BuildRunner::~BuildRunner()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

void BuildRunner::start(const QList<Step>& steps)
{
    if (m_running) return;
    m_steps = steps;
    m_index = -1;
    m_running = true;
    m_cancelled = false;
    m_currentProfile.clear();
    runNext();
}

void BuildRunner::cancel()
{
    if (!m_running) return;
    m_cancelled = true;
    if (m_process && m_process->state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
        QProcess::execute("taskkill", {"/T", "/F", "/PID", QString::number(m_process->processId())});
#else
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) m_process->kill();
#endif
    }
}

void BuildRunner::finishProfile(bool ok)
{
    if (m_currentProfile.isEmpty()) return;
    emit profileFinished(m_currentProfile, ok, m_profileTimer.elapsed());
    m_currentProfile.clear();
}

void BuildRunner::fail(const QString& message)
{
    if (!message.isEmpty()) emit output(message, Stream::Err);
    finishProfile(false);
    m_running = false;
    emit finished(false, m_cancelled);
}

void BuildRunner::runNext()
{
    if (m_cancelled) {
        fail("Cancelled.");
        return;
    }
    ++m_index;
    if (m_index >= m_steps.size()) {
        finishProfile(true);
        m_running = false;
        emit finished(true, false);
        return;
    }

    const Step& step = m_steps[m_index];
    emit stepStarted(m_index, m_steps.size(), step);

    switch (step.kind) {
    case Step::Header:
        finishProfile(true);
        m_currentProfile = step.profileName;
        m_profileTimer.start();
        runNext();
        return;
    case Step::RemovePath: {
        const QFileInfo info(step.path);
        const bool ok = !info.exists() || (info.isDir() ? QDir(step.path).removeRecursively() : QFile::remove(step.path));
        if (!ok) {
            fail("Couldn't remove " + QDir::toNativeSeparators(step.path));
            return;
        }
        runNext();
        return;
    }
    case Step::WriteFile: {
        QDir().mkpath(QFileInfo(step.path).absolutePath());
        QFile f(step.path);
        if (!f.open(QIODevice::WriteOnly)) {
            fail("Couldn't write " + QDir::toNativeSeparators(step.path));
            return;
        }
        f.close();
        runNext();
        return;
    }
    case Step::Launch: {
        QString error;
        if (!launch(step, &error)) {
            fail(error);
            return;
        }
        runNext();
        return;
    }
    case Step::Process:
        break;
    }

    emit output("> " + commandLine(step), Stream::Info);
    m_outBuffer.clear();
    m_errBuffer.clear();
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(step.workingDir);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_outBuffer += m_process->readAllStandardOutput();
        flush(m_outBuffer, Stream::Out, false);
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errBuffer += m_process->readAllStandardError();
        flush(m_errBuffer, Stream::Err, false);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        const QString program = m_process->program();
        m_process->deleteLater();
        m_process = nullptr;
        fail(QString("Couldn't start %1. Is it installed and on PATH?").arg(program));
    });
    connect(m_process, &QProcess::finished, this, &BuildRunner::processFinished);
    m_process->start(step.program, step.args);
}

void BuildRunner::flush(QByteArray& buffer, Stream stream, bool all)
{
    qsizetype newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        emit output(QString::fromLocal8Bit(line), stream);
    }
    if (all && !buffer.isEmpty()) {
        emit output(QString::fromLocal8Bit(buffer), stream);
        buffer.clear();
    }
}

void BuildRunner::processFinished(int exitCode, QProcess::ExitStatus status)
{
    m_outBuffer += m_process->readAllStandardOutput();
    m_errBuffer += m_process->readAllStandardError();
    flush(m_outBuffer, Stream::Out, true);
    flush(m_errBuffer, Stream::Err, true);
    m_process->deleteLater();
    m_process = nullptr;

    if (m_cancelled) {
        fail("Cancelled.");
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        fail(QString("%1 failed (exit code %2).").arg(m_steps[m_index].title).arg(exitCode));
        return;
    }
    runNext();
}

bool BuildRunner::launch(const Step& step, QString* error)
{
    const QString exe = step.packaged ? packagedExecutable(step.buildDir, step.configuration, step.target, error)
                                      : findTargetArtifact(step.buildDir, step.configuration, step.target, error);
    if (exe.isEmpty()) return false;
    const QString workingDir = step.launchWorkingDir.isEmpty() ? QFileInfo(exe).absolutePath() : step.launchWorkingDir;
    const QStringList args = QProcess::splitCommand(step.launchArgs);
    emit output(QString("> %1 %2").arg(QDir::toNativeSeparators(exe), step.launchArgs).trimmed(), Stream::Info);
    if (!QProcess::startDetached(exe, args, workingDir)) {
        if (error) *error = "Couldn't start " + QDir::toNativeSeparators(exe);
        return false;
    }
    return true;
}

}
