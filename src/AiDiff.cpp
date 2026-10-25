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

#include "AiDiff.h"

#include <vector>

namespace {

// Above this many line pairs the middle part is shown as a full replacement instead of an LCS diff
const qint64 MAX_LCS_CELLS = 4000000;

}

QStringList AiDiff::lines(const QString &text)
{
    return text.split('\n');
}

QList<AiDiff::Op> AiDiff::diff(const QStringList &a, const QStringList &b)
{
    // Lines shared at the start and end are unchanged; only diff the middle
    qsizetype prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
        ++prefix;
    qsizetype suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix && a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
        ++suffix;

    QList<Op> ops;
    for (qsizetype i = 0; i < prefix; ++i)
        ops.append({' ', a[i]});

    const qsizetype n = a.size() - prefix - suffix;
    const qsizetype m = b.size() - prefix - suffix;
    auto A = [&](qsizetype i) -> const QString & { return a[prefix + i]; };
    auto B = [&](qsizetype j) -> const QString & { return b[prefix + j]; };

    if (n * m <= MAX_LCS_CELLS) {
        // Longest common subsequence, lengths computed from the end
        std::vector<int> lcs((n + 1) * (m + 1), 0);
        auto at = [&](qsizetype i, qsizetype j) -> int & { return lcs[i * (m + 1) + j]; };
        for (qsizetype i = n - 1; i >= 0; --i)
            for (qsizetype j = m - 1; j >= 0; --j)
                at(i, j) = A(i) == B(j) ? at(i + 1, j + 1) + 1 : qMax(at(i + 1, j), at(i, j + 1));

        qsizetype i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && A(i) == B(j)) {
                ops.append({' ', A(i)});
                ++i;
                ++j;
            }
            else if (i < n && (j == m || at(i + 1, j) >= at(i, j + 1))) {
                ops.append({'-', A(i++)});
            }
            else {
                ops.append({'+', B(j++)});
            }
        }
    }
    else {
        for (qsizetype i = 0; i < n; ++i)
            ops.append({'-', A(i)});
        for (qsizetype j = 0; j < m; ++j)
            ops.append({'+', B(j)});
    }

    for (qsizetype i = a.size() - suffix; i < a.size(); ++i)
        ops.append({' ', a[i]});

    return ops;
}

QList<AiDiff::Hunk> AiDiff::hunks(const QList<Op> &ops)
{
    QList<Hunk> result;
    int newLine = 0;
    for (int i = 0; i < ops.size(); ++i) {
        if (ops[i].type == ' ') {
            ++newLine;
            continue;
        }
        if (result.isEmpty() || result.last().lastOp != i - 1)
            result.append(Hunk{i, i, newLine, 0, 0});

        Hunk &hunk = result.last();
        hunk.lastOp = i;
        if (ops[i].type == '+') {
            ++hunk.added;
            ++newLine;
        }
        else {
            ++hunk.removed;
        }
    }
    return result;
}

QString AiDiff::rebuild(const QList<Op> &ops, const QList<Hunk> &hunks, const QList<bool> &useNew)
{
    std::vector<int> hunkOf(ops.size(), -1);
    for (int h = 0; h < hunks.size(); ++h)
        for (int i = hunks[h].firstOp; i <= hunks[h].lastOp; ++i)
            hunkOf[i] = h;

    QStringList out;
    for (int i = 0; i < ops.size(); ++i) {
        const Op &op = ops[i];
        const bool takeNew = hunkOf[i] >= 0 && useNew.value(hunkOf[i]);
        if (op.type == ' ' || (op.type == '+' && takeNew) || (op.type == '-' && !takeNew))
            out.append(op.line);
    }
    return out.join('\n');
}
