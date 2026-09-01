#pragma once

#include <QString>

struct ProjectMetadata
{
    QString name;
    QString database{QStringLiteral("config.sqlite")};
    QString scripts{QStringLiteral("scripts")};
};

namespace ProjectMetadataFile {
bool load(const QString &path, ProjectMetadata *metadata, QString *error = nullptr);
bool save(const QString &path, const ProjectMetadata &metadata, QString *error = nullptr);
}
