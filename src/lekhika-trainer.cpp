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
#include <vector>
#include <string>
#include <algorithm>

#include <unicode/brkiter.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <QtConcurrent/QtConcurrent>
#include <QFuture>

#ifdef HAVE_SQLITE3
// Use python script(lekhika-trainer.py) to add words in db if simplicity is perfered.

  /* ---------------------------------------------------------- */
 /* Helper to validate Devanagari grapheme clusters            */
/* ---------------------------------------------------------- */

inline bool isDependentVowelSign(UChar32 c) { return c >= 0x093E && c <= 0x094C; }
inline bool isDevanagariDigit(UChar32 c) { return c >= 0x0966 && c <= 0x096F; }
inline bool isASCII(unsigned char c) { return c < 0x80; }
bool isValidDevanagariWord(const icu::UnicodeString &u) {
    if (u.isEmpty()) return false;

    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::BreakIterator> bi(
        icu::BreakIterator::createCharacterInstance(icu::Locale::getUS(), status)
        );
    if (U_FAILURE(status)) return false;

    bi->setText(u);

    int32_t clusterCount = 0;
    bool firstClusterChecked = false;

    for (int32_t start = bi->first(), end = bi->next();
         end != icu::BreakIterator::DONE;
         start = end, end = bi->next())
    {
        clusterCount++;
        icu::UnicodeString cluster = u.tempSubStringBetween(start, end);
        if (cluster.isEmpty()) continue;
        UChar32 firstCharOfCluster = cluster.char32At(0);

        // Reject non-Devanagari, digits, or joiners.
        if (isDevanagariDigit(firstCharOfCluster) || firstCharOfCluster == 0x200C || firstCharOfCluster == 0x200D) return false;
        if (firstCharOfCluster < 0x0900 || firstCharOfCluster > 0x097F) return false;

        bool isIndependentVowel = (firstCharOfCluster >= 0x0904 && firstCharOfCluster <= 0x0914);

        if (!firstClusterChecked) {
            firstClusterChecked = true;
            bool isConsonant = (firstCharOfCluster >= 0x0915 && firstCharOfCluster <= 0x0939);
            // The first character of a word must be a consonant, an independent vowel, or Om.
            if (!(isIndependentVowel || isConsonant) || isDependentVowelSign(firstCharOfCluster))
                return false;
        } else {
            // Any subsequent cluster cannot start with an independent vowel.
            if (isIndependentVowel) return false;
        }

        // Check for orphaned modifiers and invalid sequences within the cluster
        bool hasBase = false;
        UChar32 prevChar = 0; // Keep track of the previous character
        for (int32_t i = 0; i < cluster.length(); ++i) {
            UChar32 c = cluster.char32At(i);
            bool isCurrentCharIndependentVowel = (c >= 0x0904 && c <= 0x0914);

            // RULE: An independent vowel cannot follow a Halant.
            if (prevChar == 0x094D && isCurrentCharIndependentVowel) {
                return false;
            }

            if ((c >= 0x0904 && c <= 0x0914) || (c >= 0x0915 && c <= 0x0939) || c == 0x0950) {
                hasBase = true;
            }
            // RULE: An orphaned dependent vowel sign (matra) is invalid.
            if (isDependentVowelSign(c) && !hasBase) return false;

            prevChar = c;
        }
    }

    // A word should not end with a Halant/Virama
    if (!u.isEmpty() && u.char32At(u.length() - 1) == 0x094D) {
        return false;
    }

    return clusterCount > 0;
}

  /* ---------------------------------------------------------- */
 /* Multithreaded Processing Functions                         */
/* ---------------------------------------------------------- */

std::vector<std::string> validateWordsChunk(const std::vector<icu::UnicodeString> &tokens, std::atomic<bool> *stop)
{
    std::vector<std::string> validWords;
    validWords.reserve(tokens.size());
    for (const auto &token : tokens) {
        if (*stop) {
            break;
        }
        if (isValidDevanagariWord(token)) {
            std::string word;
            token.toUTF8String(word);
            validWords.push_back(word);
        }
    }
    return validWords;
}

  /* ---------------------------------------------------------- */
 /* Worker: learn file and log actions                         */
/* ---------------------------------------------------------- */
static void learnWorker(const QString &filePath,
                        QPlainTextEdit *log,
                        std::atomic<bool> *stop)
{
    std::ifstream in(filePath.toLocal8Bit().constData(), std::ios::binary);
    if (!in) {
        QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString, "ERROR: cannot read file"));
        return;
    }

    //  Get file size and calculate total chunks
    in.seekg(0, std::ios::end);
    const long long fileSize = in.tellg();
    in.seekg(0, std::ios::beg);

    const size_t targetChunkSize = 15 * 1024 * 1024; // 15MB
    // Use the smaller of the file size or the target chunk size, but don't use a zero-size chunk.
    size_t chunkSize = targetChunkSize;
    if (fileSize > 0 && static_cast<size_t>(fileSize) < targetChunkSize) {
        chunkSize = fileSize; // If file is smaller, use its size as the one and only chunk
    }
    const int totalChunks = (fileSize > 0 && chunkSize > 0) ? (fileSize + chunkSize - 1) / chunkSize : 0;
    const int threadCount = QThread::idealThreadCount();

    // Initial log
    QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString,
                                                                                  QString("Starting job...\n"
                                                                                          "  - File: %1\n"
                                                                                          "  - Size: %2 MB\n"
                                                                                          "  - Chunks: %3 (up to %4 MB each)\n"
                                                                                          "  - CPU Cores: %5")
                                                                                      .arg(filePath)
                                                                                      .arg(fileSize / (1024.0 * 1024.0), 0, 'f', 2)
                                                                                      .arg(totalChunks)
                                                                                      .arg(chunkSize / (1024.0 * 1024.0), 0, 'f', 2)
                                                                                      .arg(threadCount)
                                                                                  ));

    std::vector<char> buffer(chunkSize);
    std::string leftover;
    long long totalValidWordsFound = 0;
    long long totalInvalidWordsFound = 0;
    int chunkCount = 0;
    int added = 0, skipped = 0;
    bool dbError = false;

    DictionaryManager dm;

    while (in.read(buffer.data(), chunkSize) || in.gcount() > 0) {
        if (*stop || dbError) break;

        chunkCount++;
        size_t bytesRead = in.gcount();
        QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString,
                                                                                      QString("Processing chunk %1 of %2...").arg(chunkCount).arg(totalChunks)));

        std::string currentChunk(buffer.data(), bytesRead);
        currentChunk.insert(0, leftover);
        leftover.clear();

        if (!in.eof()) {
            size_t splitPoint = currentChunk.find_last_of(" \t\n\r");
            if (splitPoint != std::string::npos) {
                leftover = currentChunk.substr(splitPoint + 1);
                currentChunk.resize(splitPoint);
            }
        }

        icu::UnicodeString u_chunk = icu::UnicodeString::fromUTF8(currentChunk);
        UErrorCode status = U_ZERO_ERROR;
        std::unique_ptr<icu::BreakIterator> wi(icu::BreakIterator::createWordInstance(icu::Locale::getUS(), status));
        if (U_FAILURE(status)) continue;
        wi->setText(u_chunk);

        std::vector<icu::UnicodeString> chunkTokens;
        for (int32_t start = wi->first(), end = wi->next(); end != icu::BreakIterator::DONE; start = end, end = wi->next()) {
            icu::UnicodeString token = u_chunk.tempSubStringBetween(start, end);
            std::string utf8First;
            token.tempSubStringBetween(0, 1).toUTF8String(utf8First);
            if (!token.isEmpty() && (utf8First.empty() || !isASCII(utf8First[0]))) {
                chunkTokens.push_back(token);
            }
        }
        u_chunk.remove();

        std::vector<std::vector<icu::UnicodeString>> chunksForThreads(threadCount);
        for (size_t i = 0; i < chunkTokens.size(); ++i) {
            chunksForThreads[i % threadCount].push_back(chunkTokens[i]);
        }

        QList<QFuture<std::vector<std::string>>> futures;
        for (const auto& chunk : chunksForThreads) {
            if (!chunk.empty()) {
                futures.append(QtConcurrent::run(validateWordsChunk, chunk, stop));
            }
        }

        std::vector<std::string> chunkValidWords;
        for (auto& future : futures) {
            future.waitForFinished();
            if (*stop) break;
            auto result = future.result();
            chunkValidWords.insert(chunkValidWords.end(), result.begin(), result.end());
        }
        if (*stop) break;

        long long invalidInChunk = chunkTokens.size() - chunkValidWords.size();
        totalInvalidWordsFound += invalidInChunk;
        totalValidWordsFound += chunkValidWords.size();

        if (!chunkValidWords.empty()) {
            QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString,
                                                                                          QString("  -> Found %1 valid words (%2 invalid) in chunk %3. Committing to database...").arg(chunkValidWords.size()).arg(invalidInChunk).arg(chunkCount)));

            try {
                dm.beginTransaction();
                // Write this chunk's words to the DB transaction
                for (const auto& word : chunkValidWords) {
                    int freq = dm.getWordFrequency(word);
                    dm.addWord(word);
                    if (freq > 0) skipped++;
                    else added++;
                }
                dm.commitTransaction();
                QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString,
                                                                                              QString("  -> Commit successful. Total valid words processed so far: %1.").arg(totalValidWordsFound)));
            } catch (const std::exception& e) {
                dm.rollbackTransaction();
                dbError = true;
                QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString,
                                                                                              QString("  -> DATABASE ERROR on chunk %1: %2. Aborting.").arg(chunkCount).arg(e.what())));
            }
        } else {
            QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString,
                                                                                          QString("  -> No new valid words found in chunk %1.").arg(chunkCount)));
        }
    }

    if (*stop) {
        QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString, "\nOperation cancelled by user."));
        return;
    }

    if (dbError) {
        QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString, "\nOperation failed due to a database error."));
        return;
    }

    QString summary = QString("\nFinished:\n"
                              "  - Valid words found: %1\n"
                              "  - Invalid/Skipped tokens: %2\n"
                              "  - Added to DB: %3\n"
                              "  - Existing in DB (frequency updated): %4")
                          .arg(totalValidWordsFound)
                          .arg(totalInvalidWordsFound)
                          .arg(added)
                          .arg(skipped);
    QMetaObject::invokeMethod(log, "appendPlainText", Qt::QueuedConnection, Q_ARG(QString, summary));
}


  /* ---------------------------------------------------------- */
 /* Import Tab                                                 */
/* ---------------------------------------------------------- */
class ImportTab : public QWidget
{
public:
    explicit ImportTab(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *lay     = new QVBoxLayout(this);
        auto *top     = new QHBoxLayout;
        openBtn       = new QPushButton("Open text file …");
        learnBtn      = new QPushButton("Learn words");
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
        log->clear(); // Clear log for new job
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
 /* DB Editor Tab                                               */
/* ----------------------------------------------------------- */
class DbEditorTab : public QWidget
{
public:
    void refresh() { reload(); }
    explicit DbEditorTab(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *v = new QVBoxLayout(this);
        auto *topControls = new QHBoxLayout;

        reloadBtn = new QPushButton("Reload");
        newBtn    = new QPushButton("New word");
        delBtn    = new QPushButton("Delete selected");
        resetBtn  = new QPushButton("Reset DB");
        editBtn   = new QPushButton("Edit word");
        editBtn->setEnabled(false);

        topControls->addWidget(reloadBtn);
        topControls->addWidget(newBtn);
        topControls->addWidget(delBtn);
        topControls->addWidget(editBtn);
        topControls->addWidget(resetBtn);
        topControls->addStretch();

        auto *searchLay = new QHBoxLayout;
        searchEdit_ = new QLineEdit;
        searchEdit_->setPlaceholderText("Search for a word (Latin or Devanagari)...");
        clearSearchBtn_ = new QPushButton("Clear");
        searchLay->addWidget(new QLabel("Search:"));
        searchLay->addWidget(searchEdit_);
        searchLay->addWidget(clearSearchBtn_);

        table = new QTableWidget(0, 2);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setHorizontalHeaderLabels({"Word", "Frequency"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        table->setSortingEnabled(true);

        connect(table->horizontalHeader(), &QHeaderView::sectionClicked, this, &DbEditorTab::onSortColumnChanged);

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

        v->addLayout(topControls);
        v->addLayout(searchLay);
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
        connect(searchEdit_, &QLineEdit::textChanged, this, &DbEditorTab::onSearchTextChanged);
        connect(clearSearchBtn_, &QPushButton::clicked, this, [this](){ searchEdit_->clear(); });
    }

    void setOnDatabaseUpdateCallback(std::function<void()> callback) {
        onDatabaseUpdate_ = callback;
    }

private:
    void onSearchTextChanged(const QString &text) {
        if (text.isEmpty()) {
            m_isSearchActive = false;
            reload();
            return;
        }

        m_isSearchActive = true;

        Transliteration transliterator;
        std::string devanagari_search_term = transliterator.transliterate(text.toStdString());

        log->appendPlainText(QString("Searching for '%1' (%2)...").arg(text, QString::fromStdString(devanagari_search_term)));

        DictionaryManager dm;
        auto words = dm.searchWords(devanagari_search_term);

        table->setSortingEnabled(false);
        table->setRowCount(0); // Clear table before populating search results
        table->setRowCount(words.size());

        for(size_t i = 0; i < words.size(); ++i) {
            auto *wItem = new QTableWidgetItem(QString::fromStdString(words[i].first));
            auto *fItem = new QTableWidgetItem(QString::number(words[i].second));
            wItem->setFlags(wItem->flags() & ~Qt::ItemIsEditable);
            fItem->setFlags(fItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 0, wItem);
            table->setItem(i, 1, fItem);
        }
        table->setSortingEnabled(true);
        log->appendPlainText(QString("Found %1 match(es).").arg(words.size()));
    }

    void onSortColumnChanged(int logicalIndex) {
        m_currentSortColumn = logicalIndex;
        m_currentSortOrder = table->horizontalHeader()->sortIndicatorOrder();
        reload();
    }

    void onSelectionChanged() {
        int n = table->selectionModel()->selectedRows().count();
        editBtn->setEnabled(n == 1);
    }

    void reload() {
        if (m_isSearchActive) {
            onSearchTextChanged(searchEdit_->text());
            return;
        }
        log->clear();
        currentPage_ = 0;
        table->setRowCount(0);
        table->horizontalHeader()->setSortIndicator(m_currentSortColumn, m_currentSortOrder);
        log->appendPlainText("Reloading dictionary...");
        loadMore();
    }

    void loadMore() {
        if (isLoading_ || m_isSearchActive) return;
        isLoading_ = true;

        if (currentPage_ == 0) {
            log->appendPlainText("Loading initial words...");
        } else {
            log->appendPlainText(QString("Loading page %1...").arg(currentPage_ + 1));
        }

        DictionaryManager dm;
        auto sortBy = (m_currentSortColumn == 0) ? DictionaryManager::ByWord : DictionaryManager::ByFrequency;
        bool ascending = (m_currentSortOrder == Qt::AscendingOrder);
        auto words = dm.getAllWords(pageSize_, currentPage_ * pageSize_, sortBy, ascending);

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

        currentPage_++;
        isLoading_ = false;
    }

    void onScroll(int value) {
        if (!isLoading_ && !m_isSearchActive && value == table->verticalScrollBar()->maximum()) {
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

        std::sort(sel.begin(), sel.end(), [](const QModelIndex &a, const QModelIndex &b) {
            return a.row() > b.row();
        });

        DictionaryManager dm;
        for (const auto &idx : sel) {
            QString w = table->item(idx.row(), 0)->text();
            dm.removeWord(w.toStdString());
            log->appendPlainText(QString("- %1 deleted").arg(w));
            table->removeRow(idx.row());
        }

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
    QPushButton *reloadBtn, *newBtn, *delBtn, *resetBtn, *editBtn, *clearSearchBtn_;
    QLineEdit *searchEdit_;
    QPlainTextEdit *log;
    std::function<void()> onDatabaseUpdate_ = nullptr;
    int currentPage_ = 0;
    const int pageSize_ = 50;
    bool isLoading_ = false;
    bool m_isSearchActive = false;
    int m_currentSortColumn = 0;
    Qt::SortOrder m_currentSortOrder = Qt::AscendingOrder;
};

  /* ---------------------------------------------------------- */
 /* Test Tab                                                   */
/* ---------------------------------------------------------- */
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
 /* Help Tab                                                   */
/* ---------------------------------------------------------- */
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
         <li><b>Learn Words:</b> Import text files (.txt) with Devanagari texts. Extracts valid words into your dictionary (increments frequency for existing words).</li>
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
         <p>Your dictionary: <code style="background: #00f5f5; padding: 2px 4px; border-radius: 3px; font-size: 0.95em;">~/.local/share/fcitx5-lekhika/lekhikadict.akshardb</code></p>

         <hr style="border: 0; border-top: 1px solid #eee;">

         <p style="font-size: 0.9em; color: #7f8c8d;">
         <i>Licensed under GNU GPL v3 or later. Free Software Foundation.
         </p>
         </div>
         )");
        helpContent_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        helpContent_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        mainLay->addWidget(helpContent_, 1);
        dwninfo = new QLabel("If you want a head start, you can download a dictionary with pre trained common words.");
        dwninfo->setWordWrap(true);
        dwninfo->setStyleSheet("color: red;");
        auto *group = new QGroupBox("");
        auto *groupLay = new QVBoxLayout(group);
        groupLay->addWidget(dwninfo);
        downloadBtn_ = new QPushButton("Download and Replace Database");
        stopDownloadBtn_ = new QPushButton("Stop Download");
        stopDownloadBtn_->setVisible(false);
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumHeight(80);
        log_->setPlaceholderText("Log output will appear here...");

        auto *btnLay = new QHBoxLayout;
        btnLay->addWidget(downloadBtn_);
        btnLay->addWidget(stopDownloadBtn_);
        btnLay->addStretch();

        groupLay->addLayout(btnLay);
        groupLay->addWidget(log_);
        mainLay->addWidget(group, 0);

        netManager_ = new QNetworkAccessManager(this);
        connect(downloadBtn_, &QPushButton::clicked, this, &HelpTab::downloadDatabase);
        connect(stopDownloadBtn_, &QPushButton::clicked, this, &HelpTab::stopDownload);

        m_downloadTimer = new QTimer(this);
        m_downloadTimer->setSingleShot(true);
        connect(m_downloadTimer, &QTimer::timeout, this, &HelpTab::onDownloadTimeout);
    }

    void setOnDatabaseUpdateCallback(std::function<void()> callback) {
        onDatabaseUpdate_ = callback;
    }

private:
    void onDownloadTimeout() {
        if (m_currentReply) {
            log_->appendPlainText("Download timed out (no activity for 15 seconds).");
            m_currentReply->abort();
        }
    }

    void stopDownload() {
        if (m_currentReply) {
            m_downloadTimer->stop();
            m_userStopped = true;
            m_currentReply->abort();
        }
    }

    QLabel *dwninfo;
    QTextEdit *helpContent_;
    QPushButton *downloadBtn_;
    QPushButton *stopDownloadBtn_;
    QPlainTextEdit *log_;
    QNetworkAccessManager *netManager_;
    QNetworkReply *m_currentReply = nullptr;
    std::function<void()> onDatabaseUpdate_ = nullptr;
    QTimer *m_downloadTimer = nullptr;
    bool m_userStopped = false;

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
        stopDownloadBtn_->setVisible(true);
        m_userStopped = false;

        QString dirPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                          + QLatin1String("/fcitx5-lekhika");
        QDir dir(dirPath);
        if (!dir.exists() && !dir.mkpath(".")) {
            log_->appendPlainText("Error: could not create " + dirPath);
            downloadBtn_->setEnabled(true);
            stopDownloadBtn_->setVisible(false);
            return;
        }

        const QString localFile = dirPath + QLatin1String("/lekhikadict.akshardb");
        const QUrl url("https://github.com/khumnath/fcitx5-lekhika/releases/download/dictionary/lekhikadict.akshardb");
        QNetworkRequest req(url);
        m_currentReply = netManager_->get(req);
        m_downloadTimer->start(15000); // 15 second timeout

        connect(m_currentReply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
            m_downloadTimer->start(15000); // Reset timer on progress
            if (bytesTotal > 0) {
                QString progressText = QString::asprintf("Downloading: %.2f MB / %.2f MB",
                                                         bytesReceived / (1024.0 * 1024.0),
                                                         bytesTotal / (1024.0 * 1024.0));
                QMetaObject::invokeMethod(log_, [this, progressText]() {
                        QTextCursor cursor = log_->textCursor();
                        cursor.movePosition(QTextCursor::End);
                        cursor.select(QTextCursor::BlockUnderCursor);
                        cursor.removeSelectedText();
                        if (log_->toPlainText().endsWith('\n')) {
                            QTextCursor cleanupCursor = log_->textCursor();
                            cleanupCursor.movePosition(QTextCursor::End);
                            cleanupCursor.deletePreviousChar();
                        }
                        log_->appendPlainText(progressText);
                    }, Qt::QueuedConnection);
            }
        });

        connect(m_currentReply, &QNetworkReply::finished, this, [this, localFile]() {
            m_downloadTimer->stop();

            QNetworkReply::NetworkError error = m_currentReply->error();
            QString errorString = m_currentReply->errorString();
            QByteArray data;
            if (error == QNetworkReply::NoError) {
                data = m_currentReply->readAll();
            }

            m_currentReply->deleteLater();
            m_currentReply = nullptr;

            QMetaObject::invokeMethod(this, [this, error, errorString, data, localFile]() {
                    QTextCursor cursor = log_->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    cursor.select(QTextCursor::BlockUnderCursor);
                    cursor.removeSelectedText();
                    if (!log_->toPlainText().isEmpty() && !log_->toPlainText().endsWith('\n')) {
                        QTextCursor cleanupCursor = log_->textCursor();
                        cleanupCursor.movePosition(QTextCursor::End);
                        cleanupCursor.deletePreviousChar();
                    }

                    if (error == QNetworkReply::OperationCanceledError) {
                        if (!m_userStopped) {
                        } else {
                            log_->appendPlainText("Download cancelled by user.");
                        }
                    } else if (error != QNetworkReply::NoError) {
                        QString detail;
                        if (error == QNetworkReply::ContentNotFoundError)
                            detail = "Server replied: 404 – dictionary not found. database not changed.";
                        else if (error == QNetworkReply::HostNotFoundError ||
                                 error == QNetworkReply::TimeoutError)
                            detail = "No internet connection.";
                        else
                            detail = QString("Network error: %1").arg(errorString);
                        log_->appendPlainText("Download failed. " + detail);
                    } else {
                        QString tempFile = localFile + ".tmp";
                        QFile file(tempFile);
                        if (!file.open(QIODevice::WriteOnly)) {
                            log_->appendPlainText(QString("Error: cannot write temporary file %1").arg(tempFile));
                        } else {
                            file.write(data);
                            file.close();

                            QFile::remove(localFile);
                            if (QFile::rename(tempFile, localFile)) {
                                log_->appendPlainText("Success! Dictionary updated.");
                                log_->appendPlainText("Please restart fcitx5 to use the new dictionary. dictionary can be tested on settings tab without restart this application. ");
                                if (onDatabaseUpdate_) onDatabaseUpdate_();
                            } else {
                                log_->appendPlainText("Error: could not replace the old dictionary file.");
                            }
                        }
                    }

                    downloadBtn_->setEnabled(true);
                    stopDownloadBtn_->setVisible(false);
                }, Qt::QueuedConnection);
        });
    }
};


  /* ---------------------------------------------------------- */
 /* Main Window                                                */
/* ---------------------------------------------------------- */
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

        m_importTab  = new ImportTab;
        m_editTab    = new DbEditorTab;
        m_testTab = new TestTab;
        m_helpTab    = new HelpTab;

        tab->addTab(m_importTab,  "Learn Words");
        tab->addTab(m_editTab,    "Edit Dictionary");
        tab->addTab(m_testTab,    "Test");
        tab->addTab(m_helpTab,    "Help");

        connect(tab, &QTabWidget::currentChanged, this, [this, tab](int idx) {
            if (tab->widget(idx) == m_editTab) m_editTab->refresh();
        });

        setCentralWidget(tab);

        // Setup callbacks
        auto update_status_fn = [this]() {
            this->updateStatusBar();
        };

        m_importTab->setOnDatabaseUpdateCallback(update_status_fn);
        m_editTab->setOnDatabaseUpdateCallback(update_status_fn);

        m_helpTab->setOnDatabaseUpdateCallback([this](){
            m_editTab->refresh();
            this->updateStatusBar();
        });

        updateStatusBar();
    }


private:
    ImportTab* m_importTab;
    DbEditorTab* m_editTab;
    TestTab* m_testTab;
    HelpTab* m_helpTab;
    QWidget *m_statusWidget{nullptr};

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
        if (info.count("created_at"))           date      = QString::fromStdString(info["created_at"]);

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

        if (m_statusWidget) {
            statusBar()->removeWidget(m_statusWidget);
            m_statusWidget->deleteLater();
        }

        m_statusWidget = container;
        statusBar()->addPermanentWidget(m_statusWidget, 1);
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

