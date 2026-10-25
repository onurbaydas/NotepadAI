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

#include "UiIcons.h"

#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace {

const int SIZE = 16;

struct FileKind {
    QString label;
    QColor colour;
};

// Draws at 16x16 logical pixels on a 3x pixmap, so the icon stays sharp on high-DPI screens
template <typename Paint>
QIcon paintIcon(Paint paint)
{
    QIcon icon;
    for (int scale : {1, 2, 3}) {
        QPixmap pixmap(SIZE * scale, SIZE * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        paint(p);
        p.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

FileKind kindFor(const QString &fileName)
{
    static const QHash<QString, FileKind> byExtension = {
        {"py", {"py", QColor(0x4b, 0x8b, 0xbe)}},
        {"pyw", {"py", QColor(0x4b, 0x8b, 0xbe)}},
        {"js", {"js", QColor(0xd6, 0xb9, 0x2b)}},
        {"mjs", {"js", QColor(0xd6, 0xb9, 0x2b)}},
        {"cjs", {"js", QColor(0xd6, 0xb9, 0x2b)}},
        {"jsx", {"jsx", QColor(0x4f, 0xb3, 0xd9)}},
        {"ts", {"ts", QColor(0x31, 0x78, 0xc6)}},
        {"tsx", {"tsx", QColor(0x31, 0x78, 0xc6)}},
        {"json", {"{}", QColor(0xcb, 0xa6, 0x3a)}},
        {"html", {"<>", QColor(0xe3, 0x4c, 0x26)}},
        {"htm", {"<>", QColor(0xe3, 0x4c, 0x26)}},
        {"xml", {"<>", QColor(0xe3, 0x7c, 0x3b)}},
        {"css", {"#", QColor(0x56, 0x3d, 0x9c)}},
        {"scss", {"#", QColor(0xc6, 0x53, 0x8c)}},
        {"md", {"M", QColor(0x51, 0x9a, 0xba)}},
        {"c", {"C", QColor(0x55, 0x6b, 0xb5)}},
        {"h", {"h", QColor(0x8e, 0x7c, 0xc3)}},
        {"hpp", {"h", QColor(0x8e, 0x7c, 0xc3)}},
        {"cpp", {"C+", QColor(0x00, 0x59, 0x9c)}},
        {"cc", {"C+", QColor(0x00, 0x59, 0x9c)}},
        {"cs", {"C#", QColor(0x6a, 0x40, 0x9a)}},
        {"java", {"J", QColor(0xb0, 0x72, 0x19)}},
        {"kt", {"K", QColor(0xa9, 0x7b, 0xff)}},
        {"go", {"go", QColor(0x00, 0xad, 0xd8)}},
        {"rs", {"rs", QColor(0xce, 0x7b, 0x4f)}},
        {"rb", {"rb", QColor(0xcc, 0x34, 0x2d)}},
        {"php", {"php", QColor(0x77, 0x7b, 0xb4)}},
        {"lua", {"lua", QColor(0x3b, 0x5b, 0xc9)}},
        {"sh", {"$", QColor(0x4e, 0xaa, 0x25)}},
        {"bash", {"$", QColor(0x4e, 0xaa, 0x25)}},
        {"ps1", {">", QColor(0x2a, 0x6f, 0xb8)}},
        {"bat", {">", QColor(0x6e, 0x8b, 0x3d)}},
        {"cmd", {">", QColor(0x6e, 0x8b, 0x3d)}},
        {"sql", {"db", QColor(0xd8, 0x8c, 0x2e)}},
        {"yml", {"y", QColor(0xcb, 0x41, 0x71)}},
        {"yaml", {"y", QColor(0xcb, 0x41, 0x71)}},
        {"toml", {"t", QColor(0x9c, 0x42, 0x21)}},
        {"ini", {"⚙", QColor(0x7f, 0x84, 0x8e)}},
        {"cfg", {"⚙", QColor(0x7f, 0x84, 0x8e)}},
        {"env", {"⚙", QColor(0xd0, 0xa2, 0x1b)}},
        {"vue", {"V", QColor(0x41, 0xb8, 0x83)}},
        {"svelte", {"S", QColor(0xff, 0x3e, 0x00)}},
        {"dart", {"d", QColor(0x00, 0xb4, 0xab)}},
        {"swift", {"sw", QColor(0xf0, 0x51, 0x38)}},
        {"txt", {QString(), QColor(0x8b, 0x90, 0x99)}},
        {"log", {QString(), QColor(0x8b, 0x90, 0x99)}},
    };
    static const QHash<QString, FileKind> byName = {
        {"cmakelists.txt", {"cm", QColor(0x06, 0x4f, 0x8c)}},
        {"makefile", {"mk", QColor(0x6d, 0x80, 0x86)}},
        {"dockerfile", {"dk", QColor(0x38, 0x8f, 0xe5)}},
        {".gitignore", {"git", QColor(0xe0, 0x4e, 0x39)}},
        {"license", {"§", QColor(0xcb, 0xa6, 0x3a)}},
    };

    const QString lower = fileName.toLower();
    if (byName.contains(lower))
        return byName.value(lower);

    const QString suffix = QFileInfo(lower).suffix();
    return byExtension.value(suffix, {QString(), QColor(0x8b, 0x90, 0x99)});
}

QColor textColour()
{
    return QApplication::palette().color(QPalette::WindowText);
}

}

QIcon UiIcons::fileIcon(const QString &fileName)
{
    static QHash<QString, QIcon> cache;

    const FileKind kind = kindFor(fileName);
    const QString key = kind.label + kind.colour.name();
    if (cache.contains(key))
        return cache.value(key);

    QIcon icon = paintIcon([&kind](QPainter &p) {
        if (kind.label.isEmpty()) {
            // Plain page with a folded corner
            QPainterPath page;
            page.moveTo(3.5, 1.5);
            page.lineTo(9.5, 1.5);
            page.lineTo(12.5, 4.5);
            page.lineTo(12.5, 14.5);
            page.lineTo(3.5, 14.5);
            page.closeSubpath();
            p.setPen(QPen(kind.colour, 1.1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(page);
            p.drawLine(QPointF(5.5, 7.5), QPointF(10.5, 7.5));
            p.drawLine(QPointF(5.5, 10), QPointF(10.5, 10));
            return;
        }

        // Language badge: the short name in the language colour
        QFont font = QApplication::font();
        font.setBold(true);
        font.setPixelSize(kind.label.size() >= 3 ? 7 : (kind.label.size() == 2 ? 8 : 11));
        p.setFont(font);
        p.setPen(kind.colour.lighter(115));
        p.drawText(QRectF(0, 0, SIZE, SIZE), Qt::AlignCenter, kind.label);
    });

    cache.insert(key, icon);
    return icon;
}

QIcon UiIcons::folderIcon(bool open)
{
    return paintIcon([open](QPainter &p) {
        const QColor colour(0x8f, 0xa8, 0xc8);
        QPainterPath folder;
        folder.moveTo(1.5, 3.5);
        folder.lineTo(6, 3.5);
        folder.lineTo(7.5, 5);
        folder.lineTo(14.5, 5);
        folder.lineTo(14.5, 13.5);
        folder.lineTo(1.5, 13.5);
        folder.closeSubpath();
        p.setPen(QPen(colour, 1.1));
        p.setBrush(open ? QColor(colour.red(), colour.green(), colour.blue(), 70) : Qt::NoBrush);
        p.drawPath(folder);
    });
}

QIcon UiIcons::glyph(Glyph glyph, QColor colour)
{
    if (!colour.isValid())
        colour = textColour();

    return paintIcon([glyph, colour](QPainter &p) {
        QPen pen(colour, 1.2);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        auto page = [&p]() {
            QPainterPath path;
            path.moveTo(3.5, 1.5);
            path.lineTo(8.5, 1.5);
            path.lineTo(11.5, 4.5);
            path.lineTo(11.5, 7);
            path.moveTo(7, 14.5);
            path.lineTo(3.5, 14.5);
            path.lineTo(3.5, 1.5);
            p.drawPath(path);
        };
        auto plus = [&p]() {
            p.drawLine(QPointF(12, 9), QPointF(12, 15));
            p.drawLine(QPointF(9, 12), QPointF(15, 12));
        };

        switch (glyph) {
        case NewFile:
            page();
            plus();
            break;
        case NewFolder: {
            QPainterPath path;
            path.moveTo(8, 13.5);
            path.lineTo(1.5, 13.5);
            path.lineTo(1.5, 3.5);
            path.lineTo(5.5, 3.5);
            path.lineTo(7, 5);
            path.lineTo(14.5, 5);
            path.lineTo(14.5, 8);
            p.drawPath(path);
            plus();
            break;
        }
        case Refresh: {
            p.drawArc(QRectF(2.5, 2.5, 11, 11), 60 * 16, 290 * 16);
            QPainterPath arrow;
            arrow.moveTo(10, 1.5);
            arrow.lineTo(13, 3.5);
            arrow.lineTo(10.5, 6.5);
            p.drawPath(arrow);
            break;
        }
        case CollapseAll:
            p.drawRect(QRectF(4.5, 4.5, 9, 9));
            p.drawLine(QPointF(2.5, 11), QPointF(2.5, 2.5));
            p.drawLine(QPointF(2.5, 2.5), QPointF(11, 2.5));
            p.drawLine(QPointF(6.5, 9), QPointF(11.5, 9));
            break;
        case FolderOpen: {
            QPainterPath path;
            path.moveTo(1.5, 13.5);
            path.lineTo(1.5, 3.5);
            path.lineTo(5.5, 3.5);
            path.lineTo(7, 5);
            path.lineTo(12.5, 5);
            path.lineTo(12.5, 7);
            path.moveTo(1.5, 13.5);
            path.lineTo(4, 7.5);
            path.lineTo(15, 7.5);
            path.lineTo(12.5, 13.5);
            path.closeSubpath();
            p.drawPath(path);
            break;
        }
        case More:
            p.setBrush(colour);
            p.setPen(Qt::NoPen);
            for (qreal x : {3.5, 8.0, 12.5})
                p.drawEllipse(QPointF(x, 8), 1.2, 1.2);
            break;
        case Close:
            p.drawLine(QPointF(4, 4), QPointF(12, 12));
            p.drawLine(QPointF(12, 4), QPointF(4, 12));
            break;
        case Terminal:
            p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 1.5, 1.5);
            p.drawLine(QPointF(4, 6), QPointF(6.5, 8));
            p.drawLine(QPointF(6.5, 8), QPointF(4, 10));
            p.drawLine(QPointF(8, 10.5), QPointF(12, 10.5));
            break;
        case Trash:
            p.drawLine(QPointF(2.5, 4.5), QPointF(13.5, 4.5));
            p.drawLine(QPointF(6, 4.5), QPointF(6.5, 2.5));
            p.drawLine(QPointF(6.5, 2.5), QPointF(9.5, 2.5));
            p.drawLine(QPointF(9.5, 2.5), QPointF(10, 4.5));
            p.drawRect(QRectF(4, 4.5, 8, 9.5));
            break;
        case Search:
            p.drawEllipse(QRectF(2.5, 2.5, 8, 8));
            p.drawLine(QPointF(9.5, 9.5), QPointF(13.5, 13.5));
            break;
        }
    });
}

QIcon UiIcons::dotIcon(const QColor &colour)
{
    return paintIcon([colour](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(8, 8), 3.5, 3.5);
    });
}
