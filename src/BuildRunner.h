#pragma once

#include "BuildPlan.h"

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>

namespace kbl {

class BuildRunner : public QObject
{
    Q_OBJECT
public:
    enum class Stream { Out, Err, Info };

    explicit BuildRunner(QObject* parent = nullptr);
    ~BuildRunner() override;

    void start(const QList<Step>& steps);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void stepStarted(int index, int count, const kbl::Step& step);
    void output(const QString& line, kbl::BuildRunner::Stream stream);
    void profileFinished(const QString& profileName, bool ok, qint64 milliseconds);
    void finished(bool ok, bool cancelled);

private:
    void runNext();
    void fail(const QString& message);
    void flush(QByteArray& buffer, Stream stream, bool all);
    void processFinished(int exitCode, QProcess::ExitStatus status);
    bool launch(const Step& step, QString* error);
    void finishProfile(bool ok);

    QList<Step> m_steps;
    int m_index = -1;
    bool m_running = false;
    bool m_cancelled = false;
    QProcess* m_process = nullptr;
    QByteArray m_outBuffer;
    QByteArray m_errBuffer;
    QString m_currentProfile;
    QElapsedTimer m_profileTimer;
};

}
