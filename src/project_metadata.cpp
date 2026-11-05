#include "project_metadata.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

bool ProjectMetadataFile::load(const QString &path, ProjectMetadata *metadata, QString *error)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = input.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }

    const QJsonObject object = document.object();
    metadata->name = object.value(QStringLiteral("name")).toString().trimmed();
    metadata->database = object.value(QStringLiteral("database"))
                             .toString(QStringLiteral("config.sqlite"));
    metadata->scripts = object.value(QStringLiteral("scripts"))
                            .toString(QStringLiteral("scripts"));
    return !metadata->name.isEmpty();
}

bool ProjectMetadataFile::save(const QString &path, const ProjectMetadata &metadata,
                               QString *error)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = output.errorString();
        return false;
    }

    const QJsonObject object{
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("name"), metadata.name},
        {QStringLiteral("database"), metadata.database},
        {QStringLiteral("scripts"), metadata.scripts}
    };
    output.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!output.commit()) {
        if (error)
            *error = output.errorString();
        return false;
    }
    return true;
}
