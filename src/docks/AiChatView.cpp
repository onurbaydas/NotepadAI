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

#include "AiChatView.h"

#include "DarkTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct Colors {
    QString bubble, code, border, muted, errorBack, errorText, accent;
};

Colors colors()
{
    if (DarkTheme::isEnabled())
        return {"#2b2d31", "#161719", "#3a3d42", "#8b9099", "#3d2326", "#ffb4b4", "#4c8df6"};
    return {"#eef0f3", "#f6f8fa", "#d0d7de", "#6e7781", "#ffebe9", "#a40e26", "#0969da"};
}

QString inlineMarkdown(const QString &text)
{
    const Colors c = colors();
    QString html;

    // Split on backticks first so code spans are not formatted
    const QStringList parts = text.split('`');
    for (int i = 0; i < parts.size(); ++i) {
        QString part = parts[i].toHtmlEscaped();
        if (i % 2 == 1 && i < parts.size() - 1) {
            html += QString("<code style=\"background-color:%1;\">&nbsp;%2&nbsp;</code>").arg(c.bubble, part);
            continue;
        }
        if (i % 2 == 1)
            part = "`" + part;  // unmatched backtick

        static const QRegularExpression link("\\[([^\\]]+)\\]\\(([^)\\s]+)\\)");
        static const QRegularExpression bold("\\*\\*(.+?)\\*\\*");
        static const QRegularExpression italic("(?<![\\w*])[*_](?![\\s*_])(.+?)(?<![\\s*_])[*_](?![\\w*])");
        part.replace(link, QString("<a href=\"\\2\" style=\"color:%1;\">\\1</a>").arg(c.accent));
        part.replace(bold, "<b>\\1</b>");
        part.replace(italic, "<i>\\1</i>");
        html += part;
    }
    return html;
}

}

AiChatView::AiChatView(QWidget *parent) :
    QScrollArea(parent),
    dots(new QTimer(this))
{
    setObjectName("aiChatView");
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    container = new QWidget(this);
    container->setObjectName("chatContainer");
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    QWidget *messagesHost = new QWidget(container);
    messages = new QVBoxLayout(messagesHost);
    messages->setContentsMargins(0, 0, 0, 0);
    messages->setSpacing(10);
    layout->addWidget(messagesHost);

    liveBox = new QWidget(container);
    QVBoxLayout *liveLayout = new QVBoxLayout(liveBox);
    liveLayout->setContentsMargins(0, 0, 0, 0);
    liveText = makeTextLabel(QString());
    liveText->setParent(liveBox);
    liveStatus = new QLabel(liveBox);
    liveStatus->setObjectName("muted");
    liveLayout->addWidget(liveText);
    liveLayout->addWidget(liveStatus);
    liveBox->hide();
    layout->addWidget(liveBox);

    errorLabel = new QLabel(container);
    errorLabel->setObjectName("errorBox");
    errorLabel->setWordWrap(true);
    errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    errorLabel->hide();
    layout->addWidget(errorLabel);

    layout->addStretch(1);
    setWidget(container);

    dots->setInterval(400);
    connect(dots, &QTimer::timeout, this, [this]() {
        dotCount = (dotCount + 1) % 4;
        liveStatus->setText(liveStatusText + QString(".").repeated(dotCount));
    });

    refreshStyle();
}

void AiChatView::refreshStyle()
{
    const Colors c = colors();
    container->setStyleSheet(QString(R"(
#userBubble { background-color: %1; border-radius: 8px; }
#codeBlock { background-color: %2; border: 1px solid %3; border-radius: 6px; }
#muted, #codeLanguage { color: %4; }
#errorBox { background-color: %5; color: %6; border-radius: 6px; padding: 8px; }
#emptyTitle { font-size: 15pt; font-weight: 600; }
QToolButton#codeButton { border: none; color: %4; padding: 2px 6px; }
QToolButton#codeButton:hover { background-color: %3; border-radius: 4px; }
QPushButton#suggestion { text-align: left; border: 1px solid %3; border-radius: 6px; padding: 8px 10px; background-color: transparent; }
QPushButton#suggestion:hover { background-color: %1; }
)").arg(c.bubble, c.code, c.border, c.muted, c.errorBack, c.errorText));
}

QLabel *AiChatView::makeTextLabel(const QString &html)
{
    QLabel *label = new QLabel(html);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    label->setOpenExternalLinks(false);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    connect(label, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link.startsWith("open:"))
            emit openFileRequested(link.mid(5));
        else
            QDesktopServices::openUrl(QUrl(link));
    });
    return label;
}

bool AiChatView::isAtBottom() const
{
    return verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 8;
}

void AiChatView::scrollToBottomLater()
{
    QTimer::singleShot(0, this, [this]() { verticalScrollBar()->setValue(verticalScrollBar()->maximum()); });
}

void AiChatView::clearMessages()
{
    while (QLayoutItem *item = messages->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
}

void AiChatView::setConversation(const QJsonArray &history, const QMap<int, int> &checkpointFiles, bool busy, bool trimmed)
{
    refreshStyle();
    clearMessages();

    if (history.isEmpty() && !busy) {
        addEmptyState();
        return;
    }

    if (trimmed) {
        QLabel *note = new QLabel(tr("Older messages were removed to stay within the context limit."));
        note->setObjectName("muted");
        note->setWordWrap(true);
        messages->addWidget(note);
    }

    for (const auto &value : history) {
        const QJsonObject m = value.toObject();
        const QString role = m["role"].toString();
        if (role == "user") {
            const QString text = m["display"].toString().isEmpty() ? m["content"].toString() : m["display"].toString();
            const int turn = m["turn"].toInt();
            addUserMessage(text, turn, checkpointFiles.value(turn), busy);
        }
        else if (role == "assistant") {
            if (!m["content"].toString().trimmed().isEmpty())
                addAssistantText(m["content"].toString());
        }
        else if (role == "tool") {
            addToolStep(m);
        }
    }

    scrollToBottomLater();
}

void AiChatView::setLive(bool active, const QString &text, const QStringList &pendingTools)
{
    const bool atBottom = isAtBottom();

    liveBox->setVisible(active);
    if (!active) {
        dots->stop();
        return;
    }

    liveText->setVisible(!text.isEmpty());
    liveText->setText(markdownToHtml(text));

    if (!pendingTools.isEmpty())
        liveStatusText = tr("Working: %1").arg(pendingTools.join(", "));
    else if (text.isEmpty())
        liveStatusText = tr("Thinking");
    else
        liveStatusText = tr("Writing");
    liveStatus->setText(liveStatusText + QString(".").repeated(dotCount));
    if (!dots->isActive())
        dots->start();

    if (atBottom)
        scrollToBottomLater();
}

void AiChatView::setError(const QString &error)
{
    errorLabel->setText(error);
    errorLabel->setVisible(!error.isEmpty());
    if (!error.isEmpty())
        scrollToBottomLater();
}

void AiChatView::addEmptyState()
{
    QWidget *box = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->setContentsMargins(4, 24, 4, 4);
    layout->setSpacing(8);

    QLabel *title = new QLabel(tr("What should we build?"));
    title->setObjectName("emptyTitle");
    QLabel *subtitle = new QLabel(tr("The AI can read your project, change files (you approve every change) and run commands. "
                                     "Open your project with File > Open Folder as Workspace."));
    subtitle->setObjectName("muted");
    subtitle->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(8);

    const QStringList suggestions = {
        tr("Explain how this project works"),
        tr("Find and fix bugs in the open file"),
        tr("Write tests for the open file"),
    };
    for (const QString &suggestion : suggestions) {
        QPushButton *button = new QPushButton(suggestion);
        button->setObjectName("suggestion");
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, suggestion]() { emit suggestionClicked(suggestion); });
        layout->addWidget(button);
    }

    layout->addSpacing(8);
    QLabel *tips = new QLabel(tr("Ctrl+I: edit the selection with AI   ·   Tab: accept a suggestion   ·   Ctrl+L: ask here"));
    tips->setObjectName("muted");
    tips->setWordWrap(true);
    layout->addWidget(tips);

    messages->addWidget(box);
}

void AiChatView::addUserMessage(const QString &text, int turn, int restorableFiles, bool busy)
{
    QFrame *bubble = new QFrame;
    bubble->setObjectName("userBubble");
    QVBoxLayout *layout = new QVBoxLayout(bubble);
    layout->setContentsMargins(10, 8, 10, 8);
    QLabel *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label);
    messages->addWidget(bubble);

    if (restorableFiles > 0 && !busy) {
        QLabel *restore = new QLabel(QString("<a href=\"restore\" style=\"color:%1; text-decoration:none;\">↶ %2</a>")
                                         .arg(colors().muted, tr("Undo changes from here (%1 files)").arg(restorableFiles)));
        restore->setToolTip(tr("Revert the files the AI changed after this message and edit the message again"));
        connect(restore, &QLabel::linkActivated, this, [this, turn]() { emit restoreRequested(turn); });
        messages->addWidget(restore);
    }
}

void AiChatView::addAssistantText(const QString &markdown)
{
    static const QRegularExpression fence("```([^\\n`]*)\\n(.*?)(?:```|$)", QRegularExpression::DotMatchesEverythingOption);

    int pos = 0;
    auto it = fence.globalMatch(markdown);
    while (true) {
        const bool hasMatch = it.hasNext();
        const QRegularExpressionMatch match = hasMatch ? it.next() : QRegularExpressionMatch();
        const int end = hasMatch ? match.capturedStart() : markdown.size();

        const QString text = markdown.mid(pos, end - pos).trimmed();
        if (!text.isEmpty())
            messages->addWidget(makeTextLabel(markdownToHtml(text)));

        if (!hasMatch)
            break;
        pos = match.capturedEnd();

        QString code = match.captured(2);
        if (code.endsWith('\n'))
            code.chop(1);

        QFrame *block = new QFrame;
        block->setObjectName("codeBlock");
        QVBoxLayout *layout = new QVBoxLayout(block);
        layout->setContentsMargins(8, 4, 8, 8);
        layout->setSpacing(2);

        QHBoxLayout *header = new QHBoxLayout;
        QLabel *language = new QLabel(match.captured(1).trimmed().isEmpty() ? tr("code") : match.captured(1).trimmed());
        language->setObjectName("codeLanguage");
        QToolButton *copy = new QToolButton;
        copy->setObjectName("codeButton");
        copy->setText(tr("Copy"));
        QToolButton *insert = new QToolButton;
        insert->setObjectName("codeButton");
        insert->setText(tr("Insert into editor"));
        insert->setToolTip(tr("Insert at the cursor (replaces the selection)"));
        header->addWidget(language);
        header->addStretch(1);
        header->addWidget(copy);
        header->addWidget(insert);
        layout->addLayout(header);

        QLabel *body = new QLabel(code);
        body->setTextFormat(Qt::PlainText);
        body->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(body);

        connect(copy, &QToolButton::clicked, this, [copy, code]() {
            QApplication::clipboard()->setText(code);
            copy->setText(tr("Copied"));
        });
        connect(insert, &QToolButton::clicked, this, [this, code]() { emit insertCodeRequested(code); });

        messages->addWidget(block);
    }
}

void AiChatView::addToolStep(const QJsonObject &message)
{
    const QString name = message["name"].toString();
    const QString label = message["label"].toString();
    const bool error = message["is_error"].toBool();
    const QString summary = message["summary"].toString();
    const Colors c = colors();

    const QString target = label.toHtmlEscaped();
    const QString fileLink = QString("<a href=\"open:%1\" style=\"color:%2; text-decoration:none;\">%1</a>").arg(target, c.accent);

    QString text;
    if (name == "list_files")
        text = tr("Looked through the files");
    else if (name == "read_file")
        text = tr("Read %1").arg(fileLink);
    else if (name == "search")
        text = tr("Searched for %1").arg("<code>" + target + "</code>");
    else if (name == "edit_file")
        text = tr("Edited %1").arg(fileLink);
    else if (name == "write_file")
        text = tr("Wrote %1").arg(fileLink);
    else if (name == "run_command")
        text = tr("Ran %1").arg("<code>" + target + "</code>");
    else
        text = name.toHtmlEscaped();

    if (error || name == "run_command" || name == "search")
        text += " — " + summary.toHtmlEscaped();

    const QString icon = error ? QString("<span style=\"color:%1;\">✗</span>").arg(c.errorText) : QString("✓");
    QLabel *step = makeTextLabel(QString("<span style=\"color:%1;\">%2&nbsp;&nbsp;%3</span>").arg(c.muted, icon, text));
    messages->addWidget(step);
}

QString AiChatView::markdownToHtml(const QString &markdown)
{
    const Colors c = colors();
    QString html;
    QString paragraph;
    QString listTag;
    bool inCode = false;

    auto flushParagraph = [&]() {
        if (!paragraph.isEmpty()) {
            html += "<p>" + paragraph + "</p>";
            paragraph.clear();
        }
    };
    auto closeList = [&]() {
        if (!listTag.isEmpty()) {
            html += "</" + listTag + ">";
            listTag.clear();
        }
    };

    static const QRegularExpression heading("^(#{1,6})\\s+(.*)$");
    static const QRegularExpression bullet("^\\s*[-*+]\\s+(.*)$");
    static const QRegularExpression numbered("^\\s*\\d+[.)]\\s+(.*)$");

    const QStringList lines = markdown.split('\n');
    for (const QString &line : lines) {
        if (line.trimmed().startsWith("```")) {
            flushParagraph();
            closeList();
            if (!inCode)
                html += QString("<table width=\"100%\" cellpadding=\"6\" style=\"background-color:%1;\"><tr><td><pre>").arg(c.code);
            else
                html += "</pre></td></tr></table>";
            inCode = !inCode;
            continue;
        }
        if (inCode) {
            html += line.toHtmlEscaped() + "\n";
            continue;
        }

        QRegularExpressionMatch match;
        if ((match = heading.match(line)).hasMatch()) {
            flushParagraph();
            closeList();
            html += "<p><b>" + inlineMarkdown(match.captured(2)) + "</b></p>";
        }
        else if ((match = bullet.match(line)).hasMatch() || (match = numbered.match(line)).hasMatch()) {
            flushParagraph();
            const QString tag = bullet.match(line).hasMatch() ? "ul" : "ol";
            if (listTag != tag) {
                closeList();
                html += "<" + tag + " style=\"margin-left:-20px;\">";
                listTag = tag;
            }
            html += "<li>" + inlineMarkdown(match.captured(1)) + "</li>";
        }
        else if (line.trimmed().isEmpty()) {
            flushParagraph();
            closeList();
        }
        else {
            closeList();
            if (!paragraph.isEmpty())
                paragraph += "<br>";
            paragraph += inlineMarkdown(line);
        }
    }
    if (inCode)
        html += "</pre></td></tr></table>";
    flushParagraph();
    closeList();
    return html;
}
