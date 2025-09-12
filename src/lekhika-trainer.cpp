/********************************************************************
 * lekhika-trainer.cpp  –  Qt6 GUI : learn-file + threaded editor
 * Features:
 * • Import file and learn Devanagari words
 * • Add/Edit/Delete/Reset DB words with logging
 * • Multi-selection delete and word editing support
 * • Skip invalid Devanagari grapheme clusters(still need improvement)
 * • Test input method with lekhika core settings.
 * • Download pre-trained DB and display help
 ********************************************************************
Copyright (C) <2025> <Khumnath Cg/nath.khum@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>
 *******************************************************************/
#include "lekhika_core.h"

#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <atomic>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>

#include <unicode/brkiter.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>


#ifdef HAVE_SQLITE3

/* ---------------------------------------------------------- */
/* Helper to validate Devanagari grapheme clusters           */
/* -------------------------------------------------------- */
bool isValidDevanagariWord(const std::string &utf8)
{
    icu::UnicodeString u = icu::UnicodeString::fromUTF8(utf8);
    if (u.isEmpty()) return false;

    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::BreakIterator> bi(
        icu::BreakIterator::createCharacterInstance(icu::Locale::getUS(), status));
    if (U_FAILURE(status)) return false;

    bi->setText(u);
    int32_t start = bi->first();
    for (int32_t end = bi->next(); end != icu::BreakIterator::DONE; start = end, end = bi->next()) {
        icu::UnicodeString cluster = u.tempSubStringBetween(start, end);
        UChar32 first = cluster.char32At(0);

        // Check if the first character is a valid Devanagari vowel or consonant
        if (!((first >= 0x0904 && first <= 0x0914) || (first >= 0x0915 && first <= 0x0939)))
            return false;

        // A valid grapheme cluster should not end with a Halant (Virama)
        UChar32 last = cluster.char32At(cluster.length() - 1);
        if (last == 0x094D) return false;

        // Ensure all characters are within the Devanagari block
        for (int32_t i = 0; i < cluster.length(); ++i) {
            UChar32 c = cluster.char32At(i);
            if (c < 0x0900 || c > 0x097F) return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------- */
/* Worker: learn file and log actions                        */
/* -------------------------------------------------------- */
static void learnWorker(const QString &filePath,
                        QPlainTextEdit *log,
                        std::atomic<bool> *stop)
{
    std::ifstream in(filePath.toLocal8Bit().constData(), std::ios::binary);
    if (!in) {
        QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection,
                                  Q_ARG(QString, "ERROR: cannot read file"));
        return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string whole = ss.str();

    DictionaryManager dm;
    std::string cur;
    int seen = 0, added = 0, skipped = 0;


    auto flush = [&] {
        if (cur.empty()) return;
        ++seen;
        if (isValidDevanagariWord(cur)) {
            int freq = dm.getWordFrequency(cur);
            dm.addWord(cur);
            if (freq > 0) {
                ++skipped;
                QString line = QString("  -> %1 (skipped, freq++)").arg(QString::fromStdString(cur));
                QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection,
                                          Q_ARG(QString, line));
            } else {
                ++added;
                QString line = QString("  + %1").arg(QString::fromStdString(cur));
                QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection,
                                          Q_ARG(QString, line));
            }
            QThread::msleep(2);
        }
        cur.clear();
    };

    for (unsigned char c : whole) {
        if (*stop) break;
        if (std::isalnum(c) || (c & 0x80))
            cur += c;
        else
            flush();
    }
    flush();

    QString summary = QString("Finished: %1 tokens, %2 added, %3 skipped/existing.")
                          .arg(seen).arg(added).arg(skipped);
    QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection,
                              Q_ARG(QString, summary));
}

/* ---------------------------------------------------------- */
/* Import Tab                                                */
/* -------------------------------------------------------- */
class ImportTab : public QWidget
{
public:
    explicit ImportTab(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *lay      = new QVBoxLayout(this);
        auto *top      = new QHBoxLayout;
        openBtn        = new QPushButton("Open text file …");
        learnBtn       = new QPushButton("Learn words");
        learnBtn->setEnabled(false);
        top->addWidget(openBtn);
        top->addWidget(learnBtn);
        top->addStretch();

        auto *logFrame = new QWidget;
        auto *logLayout = new QVBoxLayout(logFrame);
        logLayout->setContentsMargins(0,0,0,0);
        logLayout->setSpacing(2);

        auto *logTopLayout = new QHBoxLayout;
        logLabel = new QLabel("Log:");
        stopBtn = new QPushButton("Stop");

        stopBtn->setVisible(false);
        logTopLayout->addWidget(logLabel);
        logTopLayout->addStretch();
        logTopLayout->addWidget(stopBtn);
        logLayout->addLayout(logTopLayout);

        log = new QPlainTextEdit;
        log->setReadOnly(true);
        log->setPlaceholderText("When learning, log output will appear here...");
        log->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        logLayout->addWidget(log);

        lay->addLayout(top);
        lay->addWidget(logFrame, 1);

        connect(openBtn, &QPushButton::clicked, this, &ImportTab::pickFile);
        connect(learnBtn, &QPushButton::clicked, this, &ImportTab::startLearn);
        connect(stopBtn, &QPushButton::clicked, this, [this]() {
            stopFlag = true;
            log->appendPlainText("Stopping...");
            stopBtn->setVisible(false);
        });
    }

    void setOnDatabaseUpdateCallback(std::function<void()> callback) {
        onDatabaseUpdate_ = callback;
    }

private:
    void pickFile()
    {
        currentFile = QFileDialog::getOpenFileName(this, "Pick any text file");
        if (currentFile.isEmpty()) return;
        learnBtn->setEnabled(true);
        log->clear();
        log->appendPlainText("Selected: " + currentFile);
    }

    void startLearn()
    {
        if (currentFile.isEmpty()) return;
        log->appendPlainText("Learning …");
        learnBtn->setEnabled(false);
        stopFlag = false;
        stopBtn->setVisible(true);
        logLabel->setText("Log: Learning. please wait...");
        QThread *th = QThread::create([=] { learnWorker(currentFile, log, &stopFlag); });
        connect(th, &QThread::finished, th, &QThread::deleteLater);
        connect(th, &QThread::finished, this, [=] {
            learnBtn->setEnabled(true);
            stopBtn->setVisible(false);
            logLabel->setText("Log:");
            if(onDatabaseUpdate_) onDatabaseUpdate_();
        });
        th->start();
    }

    QString currentFile;
    QPushButton *openBtn, *learnBtn, *stopBtn;
    QLabel *logLabel;
    QPlainTextEdit *log;
    std::atomic<bool> stopFlag{false};
    std::function<void()> onDatabaseUpdate_;
};

/* ----------------------------------------------------------- */
/* DB Editor Tab                                              */
/* --------------------------------------------------------- */
class DbEditorTab : public QWidget
{
public:
    void refresh() { reload(); }
    explicit DbEditorTab(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *v = new QVBoxLayout(this);
        auto *h = new QHBoxLayout;

        reloadBtn = new QPushButton("Reload");
        newBtn    = new QPushButton("New word");
        delBtn    = new QPushButton("Delete selected");
        resetBtn  = new QPushButton("Reset DB");
        editBtn   = new QPushButton("Edit word");
        editBtn->setEnabled(false);

        h->addWidget(reloadBtn);
        h->addWidget(newBtn);
        h->addWidget(delBtn);
        h->addWidget(editBtn);
        h->addWidget(resetBtn);
        h->addStretch();

        table = new QTableWidget(0, 2);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setHorizontalHeaderLabels({"Word", "Frequency"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);

        auto *logFrame = new QWidget;
        auto *logLayout = new QVBoxLayout(logFrame);
        logLayout->setContentsMargins(0,0,0,0);
        logLayout->setSpacing(2);
        auto *logLabel = new QLabel("Log:");
        logLayout->addWidget(logLabel);

        log = new QPlainTextEdit;
        log->setReadOnly(true);
        log->setMaximumHeight(120);
        logLayout->addWidget(log);

        v->addLayout(h);
        v->addWidget(table, 1);
        v->addWidget(logFrame, 0);

        /* ----------------  connections  ---------------- */
        connect(reloadBtn, &QPushButton::clicked, this, &DbEditorTab::reload);
        connect(newBtn,    &QPushButton::clicked, this, &DbEditorTab::addRow);
        connect(delBtn,    &QPushButton::clicked, this, &DbEditorTab::delRows);
        connect(resetBtn,  &QPushButton::clicked, this, &DbEditorTab::resetDb);
        connect(editBtn,   &QPushButton::clicked, this, &DbEditorTab::editRow);
        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &DbEditorTab::onSelectionChanged);
        connect(table->verticalScrollBar(), &QScrollBar::valueChanged, this, &DbEditorTab::onScroll);
    }

    void setOnDatabaseUpdateCallback(std::function<void()> callback) {
        onDatabaseUpdate_ = callback;
    }

private:
    void onSelectionChanged() {
        int n = table->selectionModel()->selectedRows().count();
        editBtn->setEnabled(n == 1);
    }

    void reload() {
        log->clear();
        currentPage_ = 0;
        table->setRowCount(0);
        log->appendPlainText("Reloading dictionary...");
        loadMore();
    }

    void loadMore() {
        if (isLoading_) return;
        isLoading_ = true;

        if (currentPage_ == 0) {
            log->appendPlainText("Loading initial words...");
        } else {
            log->appendPlainText(QString("Loading page %1...").arg(currentPage_ + 1));
        }

        DictionaryManager dm;
        auto words = dm.getAllWords(pageSize_, currentPage_ * pageSize_);

        if (words.empty()) {
            if (currentPage_ > 0) {
                log->appendPlainText("No more words to load.");
            } else {
                log->appendPlainText("Dictionary is empty.");
            }
            isLoading_ = false;
            return;
        }

        table->setSortingEnabled(false);
        int startRow = table->rowCount();
        table->setRowCount(startRow + words.size());

        for (size_t i = 0; i < words.size(); ++i) {
            auto *wItem = new QTableWidgetItem(QString::fromStdString(words[i].first));
            auto *fItem = new QTableWidgetItem(QString::number(words[i].second));
            wItem->setFlags(wItem->flags() & ~Qt::ItemIsEditable);
            fItem->setFlags(fItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(startRow + i, 0, wItem);
            table->setItem(startRow + i, 1, fItem);
        }
        table->setSortingEnabled(true);

        currentPage_++;
        isLoading_ = false;
    }

    void onScroll(int value) {
        if (isLoading_) return;
        if (value == table->verticalScrollBar()->maximum()) {
            loadMore();
        }
    }


    void addRow() {
        bool ok;
        QString w = QInputDialog::getText(this, "New word", "Devanagari word:",
                                          QLineEdit::Normal, "", &ok);
        if (!ok || w.isEmpty()) return;
        DictionaryManager dm;
        dm.addWord(w.toStdString());
        log->appendPlainText(QString("+ %1 added").arg(w));
        reload();
        if(onDatabaseUpdate_) onDatabaseUpdate_();
    }

    void delRows() {
        auto sel = table->selectionModel()->selectedRows();
        if (sel.empty()) { log->appendPlainText("Nothing selected."); return; }
        DictionaryManager dm;
        for (const auto &idx : sel) {
            QString w = table->item(idx.row(), 0)->text();
            dm.removeWord(w.toStdString());
            log->appendPlainText(QString("- %1 deleted").arg(w));
        }
        reload();
        if(onDatabaseUpdate_) onDatabaseUpdate_();
    }

    void resetDb()
    {
        QMessageBox::StandardButton ask1 = QMessageBox::question(this,
                                                                 tr("Confirm reset"),
                                                                 tr("This will delete ALL words from the dictionary.\n"
                                                                    "The action cannot be undone.\n\n"
                                                                    "Do you really want to continue?"),
                                                                 QMessageBox::Yes | QMessageBox::No,
                                                                 QMessageBox::No);
        if (ask1 != QMessageBox::Yes) {
            log->appendPlainText("Database reset cancelled by user.");
            return;
        }

        QMessageBox::StandardButton ask2 = QMessageBox::question(this,
                                                                 tr("Final confirmation"),
                                                                 tr("Are you absolutely sure?"),
                                                                 QMessageBox::Yes | QMessageBox::No,
                                                                 QMessageBox::No);
        if (ask2 != QMessageBox::Yes) {
            log->appendPlainText("Database reset cancelled by user.");
            return;
        }

        DictionaryManager dm;
        dm.reset();
        log->appendPlainText("Database reset: all words removed.");
        reload();
        if(onDatabaseUpdate_) onDatabaseUpdate_();
    }

    void editRow() {
        auto sel = table->selectionModel()->selectedRows();
        if (sel.empty()) return;
        int row = sel.first().row();
        QString oldWord = table->item(row, 0)->text();
        int freq = table->item(row, 1)->text().toInt();

        bool ok;
        QString newWord = QInputDialog::getText(this, "Edit word",
                                                "Change Devanagari word:",
                                                QLineEdit::Normal, oldWord, &ok);
        if (!ok || newWord.isEmpty() || newWord == oldWord) return;

        DictionaryManager dm;
        dm.removeWord(oldWord.toStdString());
        dm.addWord(newWord.toStdString());
        dm.updateWordFrequency(newWord.toStdString(), freq);
        log->appendPlainText(QString("'%1' → '%2'").arg(oldWord, newWord));
        reload();
        if(onDatabaseUpdate_) onDatabaseUpdate_();
    }

private:
    QTableWidget *table;
    QPushButton *reloadBtn, *newBtn, *delBtn, *resetBtn, *editBtn;
    QPlainTextEdit *log;
    std::function<void()> onDatabaseUpdate_;
    int currentPage_ = 0;
    const int pageSize_ = 50;
    bool isLoading_ = false;
};

/* ---------------------------------------------------------- */
/*    Test Tab                                               */
/* -------------------------------------------------------- */
class TestTab : public QWidget
{
public:
    explicit TestTab(QWidget *parent = nullptr) : QWidget(parent)
    {
        transliterator_ = new Transliteration();
        auto *mainLay = new QVBoxLayout(this);

        auto *group = new QGroupBox("Editor Settings");
        auto *form  = new QFormLayout(group);

        enableSmartCorrection_ = new QCheckBox;
        enableAutoCorrect_     = new QCheckBox;
        enableIndicNumbers_    = new QCheckBox;
        enableSymbols_         = new QCheckBox;
        enableSuggestions_     = new QCheckBox;
        enableLearning_        = new QCheckBox;
        suggestionLimit_       = new QSpinBox;
        suggestionLimit_->setRange(1, 100);

        form->addRow("Enable Smart Correction",        enableSmartCorrection_);
        form->addRow("Enable Auto Correct",            enableAutoCorrect_);
        form->addRow("Enable Indic Numbers",           enableIndicNumbers_);
        form->addRow("Enable Symbols Transliteration", enableSymbols_);
        form->addRow("Enable Suggestions",             enableSuggestions_);
        form->addRow("Enable Dictionary Learning",     enableLearning_);
        form->addRow("Suggestion Limit",               suggestionLimit_);
        mainLay->addWidget(group);

        auto *btnLay = new QHBoxLayout;
        QPushButton *saveBtn = new QPushButton("Save config");
        QPushButton *loadBtn = new QPushButton("Reload config");
        btnLay->addStretch();
        btnLay->addWidget(loadBtn);
        btnLay->addWidget(saveBtn);

        auto *info = new QLabel("<i>These settings affect only this tool, not the fcitx5 engine.</i>");
        info->setTextFormat(Qt::RichText);
        info->setAlignment(Qt::AlignRight);
        info->setStyleSheet("QLabel { color: palette(mid); }");

        mainLay->addWidget(info);
        mainLay->addLayout(btnLay);

        mainLay->addWidget(new QLabel("Type Latin text to test transliteration:"));
        inputEdit_ = new QLineEdit;
        inputEdit_->setPlaceholderText("Type here …");
        mainLay->addWidget(inputEdit_);

        outputText_ = new QPlainTextEdit;
        outputText_->setReadOnly(true);
        outputText_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        outputText_->setMinimumHeight(80);
        mainLay->addWidget(outputText_, 1);

        connect(saveBtn, &QPushButton::clicked, this, &TestTab::saveConfig);
        connect(loadBtn, &QPushButton::clicked, this, &TestTab::loadConfig);
        connect(inputEdit_, &QLineEdit::textChanged, this, &TestTab::onInputChanged);
        loadConfig();
    }
private:
    void saveConfig() {
        QSettings settings("Lekhika", "TrainerSettings");
        settings.setValue("EnableSmartCorrection", enableSmartCorrection_->isChecked());
        settings.setValue("EnableAutoCorrect", enableAutoCorrect_->isChecked());
        settings.setValue("EnableIndicNumbers", enableIndicNumbers_->isChecked());
        settings.setValue("EnableSymbolsTransliteration", enableSymbols_->isChecked());
        settings.setValue("EnableSuggestions", enableSuggestions_->isChecked());
        settings.setValue("EnableDictionaryLearning", enableLearning_->isChecked());
        settings.setValue("SuggestionLimit", suggestionLimit_->value());
    }

    void loadConfig() {
        QSettings settings("Lekhika", "TrainerSettings");
        enableSmartCorrection_->setChecked(settings.value("EnableSmartCorrection", true).toBool());
        enableAutoCorrect_->setChecked(settings.value("EnableAutoCorrect", true).toBool());
        enableIndicNumbers_->setChecked(settings.value("EnableIndicNumbers", true).toBool());
        enableSymbols_->setChecked(settings.value("EnableSymbolsTransliteration", true).toBool());
        enableSuggestions_->setChecked(settings.value("EnableSuggestions", true).toBool());
        enableLearning_->setChecked(settings.value("EnableDictionaryLearning", false).toBool());
        suggestionLimit_->setValue(settings.value("SuggestionLimit", 7).toInt());
        onInputChanged(inputEdit_->text());
    }

    void onInputChanged(const QString &latin) {
        if (!transliterator_) return;
        transliterator_->setEnableSmartCorrection(enableSmartCorrection_->isChecked());
        transliterator_->setEnableAutoCorrect(enableAutoCorrect_->isChecked());
        transliterator_->setEnableIndicNumbers(enableIndicNumbers_->isChecked());
        transliterator_->setEnableSymbolsTransliteration(enableSymbols_->isChecked());

        std::string devanagari_std = transliterator_->transliterate(latin.toStdString());
        QString devanagari = QString::fromStdString(devanagari_std);
        outputText_->clear();
        outputText_->appendPlainText("Transliteration: " + devanagari);

        if (enableSuggestions_->isChecked() && !latin.isEmpty()) {
            outputText_->appendPlainText("\nDB Suggestions:");
            DictionaryManager dm;
            std::vector<std::string> words = dm.findWords(devanagari_std, suggestionLimit_->value());
            if (words.empty()) {
                outputText_->appendPlainText("(no suggestions found)");
            } else {
                for (const std::string& word : words) {
                    outputText_->appendPlainText(QString::fromStdString(word));
                }
            }
        }
    }

    Transliteration* transliterator_ = nullptr;
    QCheckBox *enableSmartCorrection_, *enableAutoCorrect_, *enableIndicNumbers_, *enableSymbols_, *enableSuggestions_, *enableLearning_;
    QSpinBox  *suggestionLimit_;
    QLineEdit *inputEdit_;
    QPlainTextEdit *outputText_;
};

/* ---------------------------------------------------------- */
/* Help Tab                                                  */
/* -------------------------------------------------------- */
class HelpTab : public QWidget
{
public:
    explicit HelpTab(QWidget *parent = nullptr) : QWidget(parent) {
        auto *mainLay = new QVBoxLayout(this);
        mainLay->setContentsMargins(8, 8, 8, 8);
        mainLay->setSpacing(6);

        helpContent_ = new QTextEdit;
        helpContent_->setReadOnly(true);
        helpContent_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        helpContent_->setHtml(R"(
         <div style="font-family: sans-serif; line-height: 1.6;">
         <h2 style="font-size: 1.2em; font-weight: 600; color: #34495e;">About This Tool</h2>
         <p>Manage the dictionary for the <b>fcitx5-lekhika</b> input method engine. Train with your text files, add/delete words, and configure transliteration behavior.</p>

         <h2 style="font-size: 1.2em; font-weight: 600; color: #34495e;">Tabs Overview</h2>
         <ul style="padding-left: 20px; list-style-type: disc;">
         <li><b>Learn Words:</b> Import text files (.txt, .html) with Devanagari. Extracts valid words into your dictionary (increments frequency for existing words).</li>
         <li><b>Edit Dictionary:</b> View, add, delete, or reset your personal word database.</li>
         <li><b>Settings:</b> Configure & test transliteration engine settings.</li>
         <li><b>Help:</b> This guide + download pre-trained database.</li>
         </ul>

         <h2 style="font-size: 1.2em; font-weight: 600; color: #34495e;">Fcitx5 Plugin Setup</h2>
         <p>After installing <b>fcitx5-lekhika</b>:</p>
         <ol style="padding-left: 20px;">
         <li>Open <b>Fcitx5 Configuration</b>.</li>
         <li>Go to <b>Input Method</b> tab.</li>
         <li>Click <b>+</b> (bottom left).</li>
         <li>Uncheck “Only Show Current Language”.</li>
         <li>Search for “Lekhika”, add it.</li>
         <li>Switch using hotkey (e.g., Ctrl+Space).</li>
         </ol>

         <h2 style="font-size: 1.2em; font-weight: 600; color: #34495e;">Database Location</h2>
         <p>Your dictionary: <code style="background: #f5f5f5; padding: 2px 4px; border-radius: 3px; font-size: 0.95em;">~/.local/share/lekhika/lekhika.db</code></p>

         <hr style="border: 0; border-top: 1px solid #eee;">

         <p style="font-size: 0.9em; color: #7f8c8d;">
         <i>Licensed under GNU GPL v3 or later. Free Software Foundation.
         </p>
         </div>
         )");
        helpContent_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        mainLay->addWidget(helpContent_, 1);
        dwninfo = new QLabel("If you want a head start, you can download a dictionary with pre trained common words. (not available yet)");
        dwninfo->setWordWrap(true);
        auto *group = new QGroupBox("");
        auto *groupLay = new QVBoxLayout(group);
        groupLay->addWidget(dwninfo);
        downloadBtn_ = new QPushButton("Download and Replace Database");
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumHeight(80);
        log_->setPlaceholderText("Log output will appear here...");

        auto *btnLay = new QHBoxLayout;
        btnLay->addWidget(downloadBtn_);
        btnLay->addStretch();

        groupLay->addLayout(btnLay);
        groupLay->addWidget(log_);
        mainLay->addWidget(group, 0);

        netManager_ = new QNetworkAccessManager(this);
        connect(downloadBtn_, &QPushButton::clicked, this, &HelpTab::downloadDatabase);
    }


private:
    QLabel *dwninfo;
    void downloadDatabase()
    {
        const QString warningText =
            "This will replace your existing dictionary file.\n"
            "Any custom words or training will be lost.\n\n"
            "Do you want to continue?";

        QMessageBox::StandardButton userChoice = QMessageBox::question(
            this,
            "Replace Existing Dictionary",
            warningText,
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Cancel
            );

        if (userChoice != QMessageBox::Ok) {
            log_->appendPlainText("Download cancelled by user.");
            return;
        }

        log_->clear();
        log_->appendPlainText("Looking for fresh dictionary…");
        downloadBtn_->setEnabled(false);

        QString dirPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                          + QLatin1String("/fcitx5-lekhika");
        QDir dir(dirPath);
        if (!dir.exists() && !dir.mkpath(".")) {
            log_->appendPlainText("Error: could not create " + dirPath);
            downloadBtn_->setEnabled(true);
            return;
        }

        const QString localFile = dirPath + QLatin1String("/lekhikadict.akshardb");
        const QUrl url("https://raw.githubusercontent.com/khumnath/lekhika-devanagari-db/main/lekhikadict.akshardb");
        QNetworkRequest req(url);
        QNetworkReply *netReply = netManager_->get(req);

        connect(netReply, &QNetworkReply::finished, this, [this, netReply, localFile]() {
            if (netReply->error() != QNetworkReply::NoError) {
                QString detail;
                if (netReply->error() == QNetworkReply::ContentNotFoundError)
                    detail = "Server replied: 404 – dictionary not found. database not changed.";
                else if (netReply->error() == QNetworkReply::HostNotFoundError ||
                         netReply->error() == QNetworkReply::TimeoutError)
                    detail = "No internet connection.";
                else
                    detail = QString("Network error: %1").arg(netReply->errorString());

                log_->appendPlainText("Download failed. " + detail);
            } else {
                QString tempFile = localFile + ".tmp";
                QFile file(tempFile);
                if (!file.open(QIODevice::WriteOnly)) {
                    log_->appendPlainText(QString("Error: cannot write temporary file %1").arg(tempFile));
                } else {
                    file.write(netReply->readAll());
                    file.close();

                    QFile::remove(localFile);
                    if (QFile::rename(tempFile, localFile)) {
                        log_->appendPlainText("Success! Dictionary updated.");
                        log_->appendPlainText("Please restart fcitx5 to use the new dictionary. dictionary can be tested on settings tab without restart this application. ");
                    } else {
                        log_->appendPlainText("Error: could not replace the old dictionary file.");
                    }
                }
            }

            downloadBtn_->setEnabled(true);
            netReply->deleteLater();
        });
    }

    QTextEdit *helpContent_;
    QPushButton *downloadBtn_;
    QPlainTextEdit *log_;
    QNetworkAccessManager *netManager_;
};


/* ---------------------------------------------------------- */
/* Main Window                                               */
/* -------------------------------------------------------- */
class MainWin : public QMainWindow
{
public:
    MainWin() : QMainWindow()
    {
        setWindowTitle("Lekhika – dictionary manager");
        resize(550, 700);
        setMinimumSize(520, 480);

        auto *tab = new QTabWidget(this);
        tab->setTabPosition(QTabWidget::North);
        tab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto *tabBar = tab->tabBar();
        tab->setDocumentMode(true);
        tabBar->setExpanding(true);

        auto *importTab   = new ImportTab;
        auto *editTab     = new DbEditorTab;
        auto *testTab = new TestTab;
        auto *helpTab     = new HelpTab;

        tab->addTab(importTab,   "Learn Words");
        tab->addTab(editTab,     "Edit Dictionary");
        tab->addTab(testTab,     "Test");
        tab->addTab(helpTab,     "Help");

        connect(tab, &QTabWidget::currentChanged, this, [tab, editTab](int idx) {
            if (tab->widget(idx) == editTab) editTab->refresh();
        });

        setCentralWidget(tab);
        auto update_fn = [this]() {
            this->updateStatusBar();
        };

        importTab->setOnDatabaseUpdateCallback(update_fn);
        editTab->setOnDatabaseUpdateCallback(update_fn);

        updateStatusBar();
    }


private:
    void updateStatusBar() {
        DictionaryManager dm;
        auto info = dm.getDatabaseInfo();

        QString wordCount = "N/A";
        QString lang      = "N/A";
        QString path      = "N/A";
        QString engine    = "Error";
        QString version   = "N/A";
        QString date      = "N/A";

        if (info.count("word_count"))       wordCount = QString::fromStdString(info["word_count"]);
        if (info.count("language"))         lang      = QString::fromStdString(info["language"]).toUpper();
        if (info.count("db_path"))          path      = QDir::toNativeSeparators(QString::fromStdString(info["db_path"]));
        if (info.count("engine"))           engine    = QString::fromStdString(info["engine"]);
        if (info.count("format_version"))   version   = QString::fromStdString(info["format_version"]);
        if (info.count("created_at"))             date      = QString::fromStdString(info["created_at"]);

        QString infoText = QString(
                               "<style>"
                               "  div, span, b { margin: 0; padding: 0; line-height: 1.0; font-size: 100%; }"
                               "</style>"
                               "<div>"
                               "Engine: <b>%1</b> <span style='color:#7f8c8d;'>v%2</span> | "
                               "Words: <span style='color:orange; font-weight:bold;'>%3</span> | "
                               "Lang: <b>%4</b> | "
                               "created at: %5"
                               "</div>"
                               ).arg(engine, version, wordCount, lang, date);
        auto *infoLbl = new QLabel;
        infoLbl->setTextFormat(Qt::RichText);
        infoLbl->setText(infoText);
        infoLbl->setAlignment(Qt::AlignCenter);
        infoLbl->setContentsMargins(0, 0, 0, 0);

        auto *pathLbl = new QLabel;
        pathLbl->setTextFormat(Qt::RichText);
        pathLbl->setContentsMargins(0, 0, 0, 0);
        pathLbl->setAlignment(Qt::AlignCenter);

        QString pathText = QString(
                               "<span style='margin:0; padding:0;'>Path: </span>"
                               "<span style='color:#3498db; font-family:monospace;'>%1</span>"
                               ).arg(path);

        pathLbl->setText(pathText);

        auto *pathLineLayout = new QHBoxLayout;
        pathLineLayout->setContentsMargins(0, 0, 0, 0);
        pathLineLayout->setSpacing(0);
        pathLineLayout->addWidget(pathLbl);

        auto *pathLineWidget = new QWidget;
        pathLineWidget->setLayout(pathLineLayout);

        auto *container = new QWidget;
        auto *layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(infoLbl);
        layout->addWidget(pathLineWidget);

        statusBar()->addPermanentWidget(container, 1);


        statusBar()->clearMessage();
        statusBar()->addPermanentWidget(container, 1);
    }
};
#else  /* !HAVE_SQLITE3 */
class MainWin : public QMainWindow
{
public:
    MainWin() {
        auto *label = new QLabel("SQLite3 support is not available.\n\nPlease install the sqlite3 development package (e.g., libsqlite3-dev) and re-compile with -DHAVE_SQLITE3.");
        label->setAlignment(Qt::AlignCenter);
        setCentralWidget(label);
        setWindowTitle("Lekhika - Error");
        resize(500, 200);
    }
};
#endif



int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    MainWin w;
    w.show();
    return app.exec();
}

