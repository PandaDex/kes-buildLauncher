#pragma once

#include "BuildProfile.h"

#include <QList>
#include <QString>

namespace kbl {

class ProfileStore
{
public:
    explicit ProfileStore(QString directory);

    const QString& directory() const { return m_directory; }
    QList<BuildProfile> load(QStringList* errors = nullptr) const;
    bool save(const BuildProfile& profile, QString* error = nullptr) const;
    bool remove(const BuildProfile& profile) const;
    bool rename(const QString& oldName, const BuildProfile& profile, QString* error = nullptr) const;
    QString filePath(const BuildProfile& profile) const;
    QString uniqueName(const QString& base, const QList<BuildProfile>& existing) const;

    static QList<BuildProfile> defaults();

private:
    QString m_directory;
};

bool loadProfileFile(const QString& path, BuildProfile& out, QString* error = nullptr);

}
