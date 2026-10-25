/*
 * This file is part of NotepadAI, a fork of Notepad Next.
 *
 * NotepadAI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NotepadAI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NotepadAI.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "DarkTheme.h"

#include "ScintillaNext.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace {

const QColor EDITOR_BACK(0x1e, 0x1f, 0x22);
const QColor TEXT(0xdf, 0xe1, 0xe5);

QString originalStyleName;

QColor fromScintilla(sptr_t colour)
{
    return QColor(colour & 0xFF, (colour >> 8) & 0xFF, (colour >> 16) & 0xFF);
}

sptr_t toScintilla(const QColor &c)
{
    return c.red() | (c.green() << 8) | (c.blue() << 16);
}

// Element colours carry an alpha channel in the top byte
sptr_t toElement(const QColor &c, quint32 alpha = 0xFF)
{
    return static_cast<sptr_t>(static_cast<quint32>(toScintilla(c)) | (alpha << 24));
}

QColor darkBackground(const QColor &c)
{
    const float lightness = c.lightnessF();
    if (lightness > 0.85f)
        return EDITOR_BACK;
    if (lightness > 0.45f) {
        // Coloured highlight backgrounds: keep the hue, make them dark
        return QColor::fromHslF(qMax(0.0f, c.hslHueF()), c.hslSaturationF() * 0.6f, 0.12f + (1.0f - lightness) * 0.35f);
    }
    return c;
}

QColor lightForeground(const QColor &c)
{
    const float lightness = c.lightnessF();
    if (lightness >= 0.6f)
        return c;
    // Mirror the lightness so dark text becomes light text of the same hue
    const float adjusted = qBound(0.62f, 1.0f - lightness * 0.55f, 0.90f);
    return QColor::fromHslF(qMax(0.0f, c.hslHueF()), c.hslSaturationF(), adjusted);
}

}

bool DarkTheme::isEnabled()
{
    return QSettings().value("Theme/Dark", true).toBool();
}

void DarkTheme::setEnabled(bool enabled)
{
    QSettings().setValue("Theme/Dark", enabled);
}

void DarkTheme::applyToApplication()
{
    if (originalStyleName.isEmpty())
        originalStyleName = QApplication::style()->name();

    if (!isEnabled()) {
        QApplication::setStyle(QStyleFactory::create(originalStyleName));
        QApplication::setPalette(QApplication::style()->standardPalette());
        return;
    }

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, QColor(0x2b, 0x2d, 0x30));
    p.setColor(QPalette::WindowText, TEXT);
    p.setColor(QPalette::Base, EDITOR_BACK);
    p.setColor(QPalette::AlternateBase, QColor(0x26, 0x28, 0x2b));
    p.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2d, 0x30));
    p.setColor(QPalette::ToolTipText, TEXT);
    p.setColor(QPalette::PlaceholderText, QColor(0x7f, 0x84, 0x8e));
    p.setColor(QPalette::Text, TEXT);
    p.setColor(QPalette::Button, QColor(0x36, 0x38, 0x3c));
    p.setColor(QPalette::ButtonText, TEXT);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    p.setColor(QPalette::Link, QColor(0x58, 0x9d, 0xf6));
    p.setColor(QPalette::LinkVisited, QColor(0x9d, 0x7c, 0xd8));
    p.setColor(QPalette::Highlight, QColor(0x2e, 0x5c, 0x8a));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x44, 0x47, 0x4b));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3c, 0x40));
    p.setColor(QPalette::Mid, QColor(0x2f, 0x31, 0x34));
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1b, 0x1d));
    p.setColor(QPalette::Shadow, QColor(0x11, 0x11, 0x11));

    const QColor disabled(0x6f, 0x73, 0x7a);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x3a, 0x3c, 0x40));

    QApplication::setPalette(p);
}

QString DarkTheme::styleSheet()
{
    // Panels shared by both themes; %1.. are theme colours
    QString panels = QStringLiteral(R"(
#dockTitleBar { background-color: %1; border-bottom: 1px solid %2; }
#dockTitle { font-weight: 600; }
#dockStatus { color: %3; }
#dockTitleBar QToolButton { padding: 2px 6px; color: %3; }
#aiDockContent, #chatContainer, QScrollArea#aiChatView { background-color: %4; }
#composer { background-color: %4; border: 1px solid %2; border-radius: 10px; }
QPlainTextEdit#composerInput { background-color: transparent; border: none; }
#contextChip { color: %3; border: 1px solid %2; border-radius: 4px; padding: 1px 6px; }
#contextToggle, #modelButton { color: %3; padding: 1px 4px; }
QComboBox#modeCombo { border: 1px solid %2; border-radius: 4px; padding: 1px 8px; background-color: transparent; }
QToolButton#sendButton { background-color: %5; color: white; border: none; border-radius: 13px; min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; font-weight: bold; }
QToolButton#sendButton:hover { background-color: %6; }
QTreeView#workspaceTree { border: none; background-color: %4; }
)");

    if (!isEnabled())
        return panels.arg("#f3f4f6", "#d0d7de", "#6e7781", "#ffffff", "#0969da", "#2f81f7");

    panels = panels.arg("#2b2d30", "#3c3f41", "#8b9099", "#1e1f22", "#3d7cf0", "#5b93f5");

    return panels + QStringLiteral(R"(
QStatusBar { border-top: 1px solid #3c3f41; }
QToolBar { border: none; }
QMenu::separator { background: #3c3f41; height: 1px; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #45484d; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle:horizontal { background: #45484d; border-radius: 4px; min-width: 30px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

ads--CDockWidgetTab ads--CElidingLabel { color: #9da0a6; }

ads--CDockWidgetTab {
    background: #2b2d30;
    border: none;
    border-right: 1px solid #1e1f22;
    padding: 4px 6px 4px 8px;
}

ads--CDockWidgetTab[activeTab="true"] {
    background: #1e1f22;
    border-top: 2px solid #3d6aa8;
}

ads--CDockWidgetTab[focused="true"] {
    background: #1e1f22;
    border-top: 2px solid #4c8df6;
}

ads--CDockWidgetTab[activeTab="true"] ads--CElidingLabel { color: #dfe1e5; }

ads--CDockWidgetTab:hover[activeTab="false"] { background: #33363a; }

ads--CDockAreaTitleBar { background: #2b2d30; border-bottom: 1px solid #1e1f22; }

#tabCloseButton { background: transparent; }
#tabCloseButton:hover { background: rgba(255, 255, 255, 30); border: none; }

#QuickFindWidget {
    border-left: 1px solid #3c3f41;
    border-right: 1px solid #3c3f41;
    border-bottom: 3px solid #4c8df6;
}
)");
}

void DarkTheme::applyToEditor(ScintillaNext *editor)
{
    if (!isEnabled())
        return;

    for (int style = 0; style <= STYLE_MAX; ++style) {
        const QColor back = darkBackground(fromScintilla(editor->styleBack(style)));
        const QColor fore = lightForeground(fromScintilla(editor->styleFore(style)));
        editor->styleSetBack(style, toScintilla(back));
        editor->styleSetFore(style, toScintilla(fore));
    }

    editor->styleSetFore(STYLE_LINENUMBER, toScintilla(QColor(0x6e, 0x76, 0x81)));
    editor->styleSetBack(STYLE_LINENUMBER, toScintilla(EDITOR_BACK));
    editor->styleSetFore(STYLE_INDENTGUIDE, toScintilla(QColor(0x3b, 0x3d, 0x41)));

    editor->setElementColour(SC_ELEMENT_CARET, toElement(QColor(0xe0, 0xe0, 0xe0)));
    editor->setElementColour(SC_ELEMENT_CARET_LINE_BACK, toElement(QColor(0x26, 0x28, 0x2c)));
    editor->setElementColour(SC_ELEMENT_SELECTION_BACK, toElement(QColor(0x26, 0x4f, 0x78)));
    editor->setElementColour(SC_ELEMENT_SELECTION_ADDITIONAL_BACK, toElement(QColor(0x26, 0x4f, 0x78)));
    editor->setElementColour(SC_ELEMENT_SELECTION_INACTIVE_BACK, toElement(QColor(0x3a, 0x3d, 0x41)));
    editor->setElementColour(SC_ELEMENT_WHITE_SPACE, toElement(QColor(0x4a, 0x4a, 0x4a)));
    editor->setElementColour(SC_ELEMENT_FOLD_LINE, toElement(QColor(0x5a, 0x5a, 0x5a)));

    editor->setFoldMarginColour(true, toScintilla(EDITOR_BACK));
    editor->setFoldMarginHiColour(true, toScintilla(EDITOR_BACK));
    for (int i = SC_MARKNUM_FOLDEREND; i <= SC_MARKNUM_FOLDEROPEN; ++i) {
        editor->markerSetFore(i, toScintilla(EDITOR_BACK));
        editor->markerSetBack(i, toScintilla(QColor(0x8a, 0x8a, 0x8a)));
    }
    editor->setEdgeColour(toScintilla(QColor(0x3b, 0x3d, 0x41)));
}
