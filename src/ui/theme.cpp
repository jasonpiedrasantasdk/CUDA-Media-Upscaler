#include "theme.hpp"

namespace upscaler {

QString Theme::stylesheet()
{
    return QStringLiteral(R"(
        /* ===== Global ===== */
        QMainWindow, QWidget {
            background-color: #0d1117;
            color: #e6edf3;
            font-family: "Segoe UI", "Ubuntu", "Noto Sans", sans-serif;
            font-size: 10pt;
        }

        /* ===== Labels ===== */
        QLabel {
            color: #e6edf3;
            background: transparent;
        }
        QLabel[objectName="secondaryLabel"] {
            color: #8b949e;
            font-size: 9pt;
        }
        QLabel[objectName="valueLabel"] {
            color: #79c0ff;
            font-weight: bold;
        }
        QLabel[objectName="titleLabel"] {
            color: #e6edf3;
            font-size: 14pt;
            font-weight: bold;
        }

        /* ===== Push Buttons ===== */
        QPushButton {
            background-color: #21262d;
            color: #e6edf3;
            border: 1px solid #30363d;
            border-radius: 6px;
            padding: 8px 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #30363d;
            border-color: #8b949e;
        }
        QPushButton:pressed {
            background-color: #161b22;
        }
        QPushButton:disabled {
            background-color: #161b22;
            color: #484f58;
            border-color: #21262d;
        }
        QPushButton[objectName="accentButton"] {
            background-color: #1f6feb;
            color: #ffffff;
            border: none;
            font-size: 12pt;
            padding: 12px 20px;
        }
        QPushButton[objectName="accentButton"]:hover {
            background-color: #388bfd;
        }
        QPushButton[objectName="accentButton"]:pressed {
            background-color: #1a5fc9;
        }
        QPushButton[objectName="accentButton"]:disabled {
            background-color: #21262d;
            color: #484f58;
        }
        QPushButton[objectName="cancelButton"] {
            background-color: #21262d;
            color: #e6edf3;
        }
        QPushButton[objectName="cancelButton"]:hover {
            background-color: #f85149;
            color: #ffffff;
        }

        /* ===== Combo Box ===== */
        QComboBox {
            background-color: #161b22;
            color: #e6edf3;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 5px 10px;
            min-height: 24px;
        }
        QComboBox:hover {
            border-color: #8b949e;
        }
        QComboBox:disabled {
            background-color: #0d1117;
            color: #484f58;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 6px solid #8b949e;
            margin-right: 8px;
        }
        QComboBox QAbstractItemView {
            background-color: #161b22;
            color: #e6edf3;
            selection-background-color: #1f6feb;
            selection-color: #ffffff;
            border: 1px solid #30363d;
            outline: none;
        }

        /* ===== Tab Widget (small, nested) ===== */
        QTabWidget::pane {
            background-color: #0d1117;
            border: 1px solid #30363d;
            border-top: none;
        }
        QTabWidget > QTabBar::tab {
            background-color: #161b22;
            color: #e6edf3;
            padding: 10px 20px;
            border: 1px solid #30363d;
            border-bottom: none;
            font-weight: bold;
        }
        QTabWidget > QTabBar::tab:selected {
            background-color: #1f6feb;
            color: #ffffff;
        }
        QTabWidget > QTabBar::tab:hover:!selected {
            background-color: #21262d;
        }

        /* ===== Browser-style Big Tab Bar ===== */
        QTabBar#browserTabBar {
            background-color: #161b22;
            border: none;
            qproperty-drawBase: false;
        }
        QTabBar#browserTabBar::tab {
            background-color: #161b22;
            color: #8b949e;
            padding: 14px 28px;
            margin: 0px 1px;
            border: none;
            border-bottom: 3px solid transparent;
            font-size: 12pt;
            font-weight: bold;
            min-width: 100px;
        }
        QTabBar#browserTabBar::tab:selected {
            background-color: #0d1117;
            color: #e6edf3;
            border-bottom: 3px solid #1f6feb;
        }
        QTabBar#browserTabBar::tab:hover:!selected {
            background-color: #21262d;
            color: #e6edf3;
            border-bottom: 3px solid #30363d;
        }

        /* ===== Progress Bar ===== */
        QProgressBar {
            background-color: #161b22;
            border: 1px solid #30363d;
            border-radius: 4px;
            text-align: center;
            color: #e6edf3;
            min-height: 20px;
        }
        QProgressBar::chunk {
            background-color: #1f6feb;
            border-radius: 3px;
        }

        /* ===== Slider ===== */
        QSlider::groove:horizontal {
            background-color: #161b22;
            border: 1px solid #30363d;
            height: 8px;
            border-radius: 4px;
        }
        QSlider::handle:horizontal {
            background-color: #1f6feb;
            border: none;
            width: 18px;
            height: 18px;
            margin: -6px 0;
            border-radius: 9px;
        }
        QSlider::handle:horizontal:hover {
            background-color: #388bfd;
        }
        QSlider::handle:horizontal:disabled {
            background-color: #30363d;
        }
        QSlider::sub-page:horizontal {
            background-color: #1f6feb;
            border-radius: 4px;
        }

        /* ===== Text Edit (log) ===== */
        QTextEdit {
            background-color: #161b22;
            color: #8b949e;
            border: 1px solid #30363d;
            border-radius: 4px;
            font-family: "Consolas", "Ubuntu Mono", "DejaVu Sans Mono", monospace;
            font-size: 9pt;
            padding: 4px;
        }

        /* ===== Spin Box ===== */
        QSpinBox {
            background-color: #30363d;
            color: #e6edf3;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QSpinBox:disabled {
            background-color: #0d1117;
            color: #484f58;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            background-color: #21262d;
            border: none;
            width: 16px;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: #30363d;
        }
        QSpinBox::up-arrow {
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-bottom: 5px solid #8b949e;
        }
        QSpinBox::down-arrow {
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #8b949e;
        }

        /* ===== Line Edit ===== */
        QLineEdit {
            background-color: #30363d;
            color: #e6edf3;
            border: 1px solid #30363d;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QLineEdit:focus {
            border-color: #1f6feb;
        }
        QLineEdit:disabled {
            background-color: #0d1117;
            color: #484f58;
        }

        /* ===== Check Box ===== */
        QCheckBox {
            color: #e6edf3;
            spacing: 8px;
            background: transparent;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 1px solid #30363d;
            border-radius: 3px;
            background-color: #161b22;
        }
        QCheckBox::indicator:checked {
            background-color: #1f6feb;
            border-color: #1f6feb;
        }
        QCheckBox::indicator:hover {
            border-color: #8b949e;
        }

        /* ===== Radio Button ===== */
        QRadioButton {
            color: #e6edf3;
            spacing: 8px;
            background: transparent;
        }
        QRadioButton::indicator {
            width: 16px;
            height: 16px;
            border: 1px solid #30363d;
            border-radius: 8px;
            background-color: #161b22;
        }
        QRadioButton::indicator:checked {
            background-color: #1f6feb;
            border-color: #1f6feb;
        }
        QRadioButton::indicator:hover {
            border-color: #8b949e;
        }

        /* ===== Scroll Bar ===== */
        QScrollBar:vertical {
            background-color: #0d1117;
            width: 10px;
            border: none;
        }
        QScrollBar::handle:vertical {
            background-color: #30363d;
            border-radius: 5px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #484f58;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
        QScrollBar:horizontal {
            background-color: #0d1117;
            height: 10px;
            border: none;
        }
        QScrollBar::handle:horizontal {
            background-color: #30363d;
            border-radius: 5px;
            min-width: 30px;
        }
        QScrollBar::handle:horizontal:hover {
            background-color: #484f58;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
        }

        /* ===== Group Box ===== */
        QGroupBox {
            background-color: #0d1117;
            border: 1px solid #30363d;
            border-radius: 6px;
            margin-top: 12px;
            padding-top: 16px;
            font-weight: bold;
            color: #e6edf3;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 2px 8px;
            color: #8b949e;
        }
    )");
}

} // namespace upscaler
