/*
    SPDX-FileCopyrightText: 2004 Duncan Mac-Vicar Prett <duncan@kde.org>
    SPDX-FileCopyrightText: 2004-2005 Olivier Goffart <ogoffart@kde.org>
    SPDX-FileCopyrightText: 2011 Niels Ole Salscheider
    <niels_ole@salscheider-online.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "latexrenderer.h"

#include <QDebug>

#include <KProcess>

#include "core/processbudget_p.h"
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>

#include "gui/debug_ui.h"

namespace GuiUtils
{
LatexRenderer::LatexRenderer()
{
}

LatexRenderer::~LatexRenderer()
{
    for (const QString &file : std::as_const(m_fileList)) {
        QFile::remove(file);
    }
}

LatexRenderer::Error LatexRenderer::renderLatexInHtml(QString &html, const QColor &textColor, int fontSize, int resolution, QString &latexOutput)
{
    if (!html.contains(QStringLiteral("$$"))) {
        return NoError;
    }

    // this searches for $$formula$$
    static const QRegularExpression rg(QStringLiteral("\\$\\$.+?\\$\\$"));
    QRegularExpressionMatchIterator matchIt = rg.globalMatch(html);

    QMap<QString, QString> replaceMap;
    while (matchIt.hasNext()) {
        QRegularExpressionMatch match = matchIt.next();
        const QString matchedString = match.captured(0);

        QString formul = matchedString;
        // first remove the $$ delimiters on start and end
        formul.remove(QStringLiteral("$$"));
        // then trim the result, so we can skip totally empty/whitespace-only formulas
        formul = formul.trimmed();
        if (formul.isEmpty() || !securityCheck(formul)) {
            continue;
        }

        // unescape formula
        formul.replace(QLatin1String("&gt;"), QLatin1String(">"));
        formul.replace(QLatin1String("&lt;"), QLatin1String("<"));
        formul.replace(QLatin1String("&amp;"), QLatin1String("&"));
        formul.replace(QLatin1String("&quot;"), QLatin1String("\""));
        formul.replace(QLatin1String("&apos;"), QLatin1String("\'"));
        formul.replace(QLatin1String("<br>"), QLatin1String(" "));

        QString fileName;
        Error returnCode = handleLatex(fileName, formul, textColor, fontSize, resolution, latexOutput);
        if (returnCode != NoError) {
            return returnCode;
        }

        replaceMap[matchedString] = fileName;
    }

    if (replaceMap.isEmpty()) { // we haven't found any LaTeX strings
        return NoError;
    }

    int imagePxWidth, imagePxHeight;
    for (QMap<QString, QString>::ConstIterator it = replaceMap.constBegin(); it != replaceMap.constEnd(); ++it) {
        QImage theImage(*it);
        if (theImage.isNull()) {
            continue;
        }
        imagePxWidth = theImage.width();
        imagePxHeight = theImage.height();
        QString escapedLATEX = it.key().toHtmlEscaped().replace(QLatin1Char('"'), QLatin1String("&quot;")); // we need  the escape quotes because that string will be in a title="" argument, but not the \n
        html.replace(it.key(),
                     QStringLiteral(" <img width=\"") + QString::number(imagePxWidth) + QStringLiteral("\" height=\"") + QString::number(imagePxHeight) + QStringLiteral("\" align=\"middle\" src=\"") + (*it) + QStringLiteral("\"  alt=\"") +
                         escapedLATEX + QStringLiteral("\" title=\"") + escapedLATEX + QStringLiteral("\"  /> "));
    }
    return NoError;
}

bool LatexRenderer::mightContainLatex(const QString &text)
{
    if (!text.contains(QStringLiteral("$$"))) {
        return false;
    }

    // this searches for $$formula$$
    static const QRegularExpression rg(QStringLiteral("\\$\\$.+?\\$\\$"));
    if (!rg.match(text).hasMatch()) {
        return false;
    }

    return true;
}

LatexRenderer::Error LatexRenderer::handleLatex(QString &fileName, const QString &latexFormula, const QColor &textColor, int fontSize, int resolution, QString &latexOutput)
{
    QTemporaryDir directory;
    if (!directory.isValid()) {
        return LatexFailed;
    }
    const QString base = directory.filePath(QStringLiteral("formula"));
    QFile source(base + QStringLiteral(".tex"));
    if (!source.open(QIODevice::WriteOnly)) {
        return LatexFailed;
    }
    QTextStream stream(&source);
    stream << "\\documentclass[" << qBound(8, fontSize, 24) << "pt]{article}\n"
           << "\\usepackage{color,amsmath,latexsym,amsfonts,amssymb,ulem}\n"
           << "\\pagestyle{empty}\\begin{document}\n"
           << "{\\color[rgb]{" << textColor.redF() << "," << textColor.greenF() << "," << textColor.blueF() << "}\\begin{eqnarray*}\n"
           << latexFormula << "\n\\end{eqnarray*}}\\end{document}\n";
    stream.flush();
    source.close();
    auto monitor = [&]() {
        qint64 size = 0;
        int count = 0;
        QDirIterator files(directory.path(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            files.next();
            size += files.fileInfo().size();
            if (++count > 100 || size > 64 * 1024 * 1024) {
                return false;
            }
        }
        return true;
    };
    QProcess latexProc, dvipngProc;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("openin_any"), QStringLiteral("p"));
    environment.insert(QStringLiteral("openout_any"), QStringLiteral("p"));
    environment.insert(QStringLiteral("TEXMFOUTPUT"), directory.path());
    for (QProcess *process : {&latexProc, &dvipngProc}) {
        process->setWorkingDirectory(directory.path());
        process->setProcessEnvironment(environment);
    }
    QString latexExecutable = QStandardPaths::findExecutable(QStringLiteral("latex"));
    if (latexExecutable.isEmpty()) {
        qCDebug(OkularUiDebug) << "Could not find latex!";
        fileName = QString();
        return LatexNotFound;
    }
    latexProc.setProgram(latexExecutable);
    latexProc.setArguments({QStringLiteral("-no-shell-escape"), QStringLiteral("-interaction=nonstopmode"), QStringLiteral("-halt-on-error"), source.fileName()});
    QByteArray output, errors;
    const int latexStatus = Okular::runBoundedHelper(latexProc, output, errors, 10000, monitor);
    latexOutput = QString::fromLocal8Bit(output + errors);
    if (latexStatus != 0 || !QFile::exists(base + QStringLiteral(".dvi"))) {
        return LatexFailed;
    }
    QString dvipngExecutable = QStandardPaths::findExecutable(QStringLiteral("dvipng"));
    if (dvipngExecutable.isEmpty()) {
        qCDebug(OkularUiDebug) << "Could not find dvipng!";
        fileName = QString();
        return DvipngNotFound;
    }

    dvipngProc.setProgram(dvipngExecutable);
    dvipngProc.setArguments(
        {QStringLiteral("-o") + base + QStringLiteral(".png"), QStringLiteral("-Ttight"), QStringLiteral("-bgTransparent"), QStringLiteral("-D"), QString::number(qBound(36, resolution, 600)), base + QStringLiteral(".dvi")});
    output.clear();
    errors.clear();
    if (Okular::runBoundedHelper(dvipngProc, output, errors, 10000, monitor) != 0 || !QFile::exists(base + QStringLiteral(".png"))) {
        return DvipngFailed;
    }
    QTemporaryFile image;
    if (!image.open()) {
        return DvipngFailed;
    }
    const QString target = image.fileName();
    image.close();
    image.remove();
    if (!QFile::copy(base + QStringLiteral(".png"), target)) {
        return DvipngFailed;
    }
    image.setAutoRemove(false);
    fileName = target;
    m_fileList << fileName;
    return NoError;
}

bool LatexRenderer::securityCheck(const QString &latexFormula)
{
    if (latexFormula.size() > 8192 || latexFormula.contains(QLatin1String("^^")) || latexFormula.contains(QLatin1Char('%')) || latexFormula.contains(QChar(0))) {
        return false;
    }
    static const QSet<QString> commands = []() {
        const QString words = QStringLiteral(
            "frac dfrac tfrac sqrt text textrm textit textbf mathrm mathit mathbf mathsf mathtt mathcal mathbb mathfrak operatorname "
            "alpha beta gamma delta epsilon varepsilon zeta eta theta vartheta iota kappa lambda mu nu xi pi varpi rho varrho sigma varsigma tau upsilon phi varphi chi psi omega "
            "Gamma Delta Theta Lambda Xi Pi Sigma Upsilon Phi Psi Omega "
            "sum prod coprod int iint iiint oint lim limsup liminf inf sup min max sin cos tan cot sec csc sinh cosh tanh log ln exp det gcd "
            "left right middle big Big bigg Bigg bigl bigr Bigl Bigr biggl biggr Biggl Biggr "
            "cdot times div pm mp ast star circ bullet cap cup subset supset subseteq supseteq in notin ni le ge leq geq neq ne approx equiv sim simeq cong propto "
            "infty partial nabla forall exists neg land lor wedge vee to mapsto leftarrow rightarrow leftrightarrow Leftarrow Rightarrow Leftrightarrow "
            "ldots cdots vdots ddots dots vec hat bar overline underline dot ddot tilde widetilde widehat overbrace underbrace "
            "quad qquad hspace vspace thinspace ensuremath boldsymbol binom dbinom tbinom emptyset varnothing ell Re Im angle perp parallel "
            "langle rangle lbrace rbrace lvert rvert lVert rVert vert Vert backslash begin end");
        const QStringList list = words.split(QLatin1Char(' '));
        return QSet<QString>(list.begin(), list.end());
    }();
    int depth = 0;
    for (qsizetype i = 0; i < latexFormula.size(); ++i) {
        const QChar c = latexFormula.at(i);
        if (c == QLatin1Char('{') && ++depth > 64)
            return false;
        if (c == QLatin1Char('}') && --depth < 0)
            return false;
        if (c != QLatin1Char('\\'))
            continue;
        if (++i == latexFormula.size())
            return false;
        if (!latexFormula.at(i).isLetter()) {
            if (!QStringLiteral("\\{}_,;:! #&$|").contains(latexFormula.at(i)))
                return false;
            continue;
        }
        const qsizetype start = i;
        while (i < latexFormula.size() && latexFormula.at(i).isLetter())
            ++i;
        const QString command = latexFormula.mid(start, i - start);
        if (!commands.contains(command))
            return false;
        if (command == QLatin1String("begin") || command == QLatin1String("end")) {
            if (i >= latexFormula.size() || latexFormula.at(i) != QLatin1Char('{'))
                return false;
            const qsizetype close = latexFormula.indexOf(QLatin1Char('}'), i);
            const QString name = latexFormula.mid(i + 1, close - i - 1);
            if (!QStringList {QStringLiteral("matrix"),
                              QStringLiteral("pmatrix"),
                              QStringLiteral("bmatrix"),
                              QStringLiteral("Bmatrix"),
                              QStringLiteral("vmatrix"),
                              QStringLiteral("Vmatrix"),
                              QStringLiteral("cases"),
                              QStringLiteral("aligned"),
                              QStringLiteral("gathered")}
                     .contains(name))
                return false;
        }
        --i;
    }
    return depth == 0;
}

}
