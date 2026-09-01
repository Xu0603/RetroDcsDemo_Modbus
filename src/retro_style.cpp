#include "retro_style.h"

#include <QtWidgets>

namespace {
const char *kRetroStyle = R"(
* { font-family: "Songti SC", "SimSun", serif; font-size: 12px; color: #000000; }
QMainWindow, QDialog, QWidget { background: #c0c0c0; }
QMenuBar { background:#c0c0c0; border-bottom:1px solid #808080; spacing:4px; }
QMenuBar::item { padding:4px 8px; background:transparent; }
QMenuBar::item:selected, QMenu::item:selected { background:#000080; color:white; }
QMenu { background:#c0c0c0; border-top:2px solid white; border-left:2px solid white; border-right:2px solid #404040; border-bottom:2px solid #404040; }
QMenu::item { padding:4px 26px 4px 22px; }
QToolBar { background:#c0c0c0; border:0; border-bottom:1px solid #808080; spacing:2px; padding:3px; }
QToolButton, QPushButton { background:#c0c0c0; border-top:2px solid white; border-left:2px solid white; border-right:2px solid #404040; border-bottom:2px solid #404040; padding:3px 7px; }
QToolButton:hover, QPushButton:hover { background:#d4d0c8; }
QToolButton:pressed, QPushButton:pressed { border-top:2px solid #404040; border-left:2px solid #404040; border-right:2px solid white; border-bottom:2px solid white; padding-left:8px; padding-top:4px; }
QLineEdit, QSpinBox, QComboBox, QTextEdit, QPlainTextEdit { background:white; border-top:2px solid #404040; border-left:2px solid #404040; border-right:2px solid white; border-bottom:2px solid white; padding:2px; }
QTreeWidget, QTableWidget, QTableView { background:white; alternate-background-color:#f1f1e8; border-top:2px solid #404040; border-left:2px solid #404040; border-right:2px solid white; border-bottom:2px solid white; gridline-color:#808080; selection-background-color:#000080; selection-color:white; }
QHeaderView::section { background:#c0c0c0; border-top:1px solid white; border-left:1px solid white; border-right:1px solid #404040; border-bottom:1px solid #404040; padding:3px; }
QTabWidget::pane { border-top:2px solid white; border-left:2px solid white; border-right:2px solid #404040; border-bottom:2px solid #404040; }
QTabBar::tab { background:#c0c0c0; border-top:2px solid white; border-left:2px solid white; border-right:2px solid #404040; padding:5px 12px; }
QTabBar::tab:selected { background:#e4e4e4; }
QGroupBox { border:1px solid #808080; margin-top:8px; padding-top:7px; }
QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 3px; }
QDockWidget { titlebar-close-icon:none; titlebar-normal-icon:none; }
QDockWidget::title { background:#000080; color:white; padding:4px; font-weight:bold; }
QStatusBar { background:#c0c0c0; border-top:1px solid white; }
QStatusBar::item { border-top:1px solid #808080; border-left:1px solid #808080; border-right:1px solid white; border-bottom:1px solid white; }
QSplitter::handle { background:#c0c0c0; }
QProgressBar { background:white; border:2px inset #c0c0c0; text-align:center; }
QProgressBar::chunk { background:#000080; }
)";
}

void applyRetroStyle(QApplication &app)
{
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(QString::fromLatin1(kRetroStyle));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#c0c0c0"));
    palette.setColor(QPalette::Button, QColor("#c0c0c0"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Highlight, QColor("#000080"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(palette);
}

QIcon pixelIcon(const QColor &color, int kind)
{
    QPixmap pm(16, 16); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing, false); p.setPen(Qt::black); p.setBrush(color);
    if (kind == 0) { p.drawRect(1, 4, 13, 10); p.setBrush(color.lighter(135)); p.drawRect(2, 2, 6, 3); }
    else if (kind == 1) { p.drawRect(2, 1, 11, 13); p.setBrush(Qt::white); p.drawRect(4, 3, 7, 4); p.setBrush(QColor("#808080")); p.drawRect(4, 9, 7, 4); }
    else if (kind == 2) { p.drawEllipse(2, 2, 11, 11); p.setBrush(Qt::white); p.drawRect(6, 4, 3, 5); p.drawRect(7, 10, 1, 1); }
    else if (kind == 3) { p.drawRect(1, 2, 13, 10); p.setPen(Qt::white); p.drawLine(3, 5, 12, 5); p.drawLine(3, 8, 9, 8); p.setPen(Qt::black); p.drawLine(4, 14, 11, 14); }
    else { p.drawRect(2, 2, 11, 11); p.setBrush(Qt::white); p.drawRect(4, 4, 7, 7); p.setPen(color); p.drawLine(5, 7, 7, 9); p.drawLine(7, 9, 10, 5); }
    p.setPen(Qt::white); p.drawPoint(2, 5); return QIcon(pm);
}

QLabel *sunkenLabel(const QString &text, int minWidth)
{
    auto *label = new QLabel(text);
    label->setMinimumWidth(minWidth);
    label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    label->setMargin(2);
    return label;
}
