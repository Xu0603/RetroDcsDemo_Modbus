#pragma once

#include <QStringList>

namespace CsvUtils {
QStringList parseRow(const QString &line, bool *ok);
QString quote(const QString &value);
}
