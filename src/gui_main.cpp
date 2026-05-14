#include "MainWindow.h"
#include "ProcessRunner.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPalette>
#include <QStyleFactory>

namespace {

// Custom dark palette — used as the foundation under the QSS layer.
// Setting the palette gives consistent colors for native controls
// that QSS doesn't fully restyle (file dialogs, message boxes).
QPalette makeDarkPalette() {
    QPalette p;
    const QColor base(30, 30, 30);
    const QColor altBase(37, 37, 37);
    const QColor window(24, 24, 24);
    const QColor text(224, 224, 224);
    const QColor disabled(120, 120, 120);
    const QColor highlight(46, 125, 50);   // matches our green accent

    p.setColor(QPalette::Window,          window);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            base);
    p.setColor(QPalette::AlternateBase,   altBase);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          QColor(45, 45, 45));
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::ToolTipBase,     base);
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::Highlight,       highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link,            QColor(102, 187, 106));

    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text,       disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    return p;
}

// App-wide stylesheet. Polish layer on top of the palette: rounded
// corners, hover/focus states, the prominent green Extract button.
const char* kAppStyleSheet = R"(
    QMainWindow, QDialog { background-color: #181818; }

    QGroupBox {
        border: 1px solid #333;
        border-radius: 8px;
        margin-top: 14px;
        padding: 16px 12px 12px 12px;
        font-weight: bold;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 12px;
        padding: 0 6px;
        color: #66BB6A;
    }

    QLabel { background: transparent; }

    QPushButton {
        background-color: #2d2d2d;
        color: #e0e0e0;
        border: 1px solid #4a4a4a;
        border-radius: 4px;
        padding: 6px 14px;
    }
    QPushButton:hover {
        background-color: #3a3a3a;
        border-color: #66BB6A;
    }
    QPushButton:pressed   { background-color: #1f1f1f; }
    QPushButton:disabled  { color: #6a6a6a; border-color: #2a2a2a; }

    /* The Extract/Cancel button is the main call-to-action. */
    QPushButton#extractButton {
        background-color: #2E7D32;
        color: white;
        font-weight: bold;
        border: 1px solid #43A047;
        padding: 8px 20px;
    }
    QPushButton#extractButton:hover    { background-color: #388E3C; }
    QPushButton#extractButton:pressed  { background-color: #1B5E20; }
    QPushButton#extractButton:disabled { background-color: #2a3d2c; color: #8aa68b; }

    QLineEdit, QTextEdit, QPlainTextEdit {
        background-color: #1e1e1e;
        color: #e0e0e0;
        border: 1px solid #3c3c3c;
        border-radius: 4px;
        padding: 4px 8px;
        selection-background-color: #2E7D32;
    }
    QLineEdit:focus, QTextEdit:focus { border-color: #66BB6A; }
    QLineEdit:disabled                { color: #777; border-color: #2a2a2a; }

    QComboBox {
        background-color: #2d2d2d;
        color: #e0e0e0;
        border: 1px solid #4a4a4a;
        border-radius: 4px;
        padding: 4px 8px;
        min-height: 22px;
    }
    QComboBox:hover         { border-color: #66BB6A; }
    QComboBox::drop-down    { border: none; width: 18px; }
    QComboBox QAbstractItemView {
        background-color: #1e1e1e;
        color: #e0e0e0;
        selection-background-color: #2E7D32;
        border: 1px solid #3c3c3c;
    }

    QListWidget {
        background-color: #1a1a1a;
        color: #e0e0e0;
        border: 1px solid #3c3c3c;
        border-radius: 4px;
        padding: 4px;
    }
    QListWidget::item          { padding: 4px 6px; border-radius: 2px; }
    QListWidget::item:hover    { background-color: #252525; }
    QListWidget::item:selected { background-color: #2E7D32; color: white; }

    QCheckBox        { color: #e0e0e0; spacing: 6px; }
    QCheckBox:disabled { color: #6a6a6a; }

    QStatusBar       { background-color: #181818; color: #b0b0b0; }
    QToolTip         { color: #e0e0e0; background-color: #2d2d2d; border: 1px solid #4a4a4a; }
    QScrollBar:vertical {
        background: #1a1a1a; width: 10px; margin: 0;
    }
    QScrollBar::handle:vertical {
        background: #3a3a3a; border-radius: 5px; min-height: 30px;
    }
    QScrollBar::handle:vertical:hover { background: #66BB6A; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)";

} // anonymous namespace


int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QApplication::setApplicationName("Subtitle Extractor");
    QApplication::setOrganizationName("Halit");
    QApplication::setApplicationVersion("1.0");
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(makeDarkPalette());
    app.setStyleSheet(QString::fromLatin1(kAppStyleSheet));

    subext::ProcessRunner::setBundledBinaryDir(
        QCoreApplication::applicationDirPath().toStdString());

    subext::MainWindow window;
    window.show();

    return app.exec();
}
