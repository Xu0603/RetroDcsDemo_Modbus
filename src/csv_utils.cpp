#include "csv_utils.h"

QStringList CsvUtils::parseRow(const QString &line, bool *ok)
{
    QStringList fields; QString field; bool quoted = false; *ok = true;
    for (int index = 0; index < line.size(); ++index) {
        const QChar ch = line.at(index);
        if (quoted) {
            if (ch == QLatin1Char('"') && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"')) {
                field += QLatin1Char('"'); ++index;
            } else if (ch == QLatin1Char('"')) quoted = false;
            else field += ch;
        } else if (ch == QLatin1Char(',')) { fields << field; field.clear(); }
        else if (ch == QLatin1Char('"') && field.isEmpty()) quoted = true;
        else field += ch;
    }
    if (quoted) *ok = false;
    fields << field;
    return fields;
}

QString CsvUtils::quote(const QString &input)
{
    QString value = input;
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}
