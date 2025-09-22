/********************************************************************
 * lekhika-core.cpp  –  lekhika core implementation.
 * Features:
 * • Transliterates latin text to devnagari.
 * • Uses Database to find suggestions.
 * • Rules based transliteration with number and symbols.
 * • Mapping and autocorrect loads from file.
 * • Self contained transliterator with header. can be used elsewhere with changing fcitx filesystem includes.
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
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/fs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
#include <sstream>

#ifdef HAVE_SQLITE3
#include <sqlite3.h>
#include <stdexcept>

// Helper function to create directories recursively
void createDirectoriesRecursive(const std::string &path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path.c_str());
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

// Helper function to check for file existence
bool fileExists(const std::string &path) {
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0);
}


  //=============================================================================//
 // DictionaryManager Implementation                                            //
//=============================================================================//

DictionaryManager::DictionaryManager() : db_(nullptr) {
    std::string dbDir =
        fcitx::StandardPath::global().userDirectory(fcitx::StandardPath::Type::Data) +
        "/fcitx5-lekhika";
    createDirectoriesRecursive(dbDir);
    std::string dbPath = dbDir + "/lekhikadict.akshardb";

    bool dbExists = fileExists(dbPath);

    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
        return;
    }

    if (!dbExists) {
        initializeDatabase();
    }
}

DictionaryManager::~DictionaryManager() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void DictionaryManager::reset()
{
    if (!db_) return;
    const char* sql = "DELETE FROM words;";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to reset dictionary: " << sqlite3_errmsg(db_) << std::endl;
    }
}


void DictionaryManager::initializeDatabase() {
    const char *sql =
        // Main word frequency table
        "CREATE TABLE IF NOT EXISTS words ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "word TEXT NOT NULL UNIQUE,"
        "frequency INTEGER NOT NULL DEFAULT 1);"
        "CREATE INDEX IF NOT EXISTS idx_word ON words(word);"

        // Metadata table for versioning and identification
        "CREATE TABLE IF NOT EXISTS meta ("
        "key TEXT PRIMARY KEY,"
        "value TEXT);"

        // Insert initial metadata entries
        "INSERT OR IGNORE INTO meta (key, value) VALUES ('format_version', '1.0');"
        "INSERT OR IGNORE INTO meta (key, value) VALUES ('engine', 'lekhila');"
        "INSERT OR IGNORE INTO meta (key, value) VALUES ('type', 'word_frequency');"
        "INSERT OR IGNORE INTO meta (key, value) VALUES ('language', 'ne');"
        "INSERT OR IGNORE INTO meta (key, value) VALUES ('script', 'Devanagari');"
        "INSERT OR IGNORE INTO meta (key, value) VALUES ('created_at', datetime('now'));";

    char *errMsg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "SQL error during initialization: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
}

std::map<std::string, std::string> DictionaryManager::getDatabaseInfo() {
    if (!db_) return {};
    std::map<std::string, std::string> info;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM words;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            info["word_count"] = std::to_string(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db_, "SELECT key, value FROM meta;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            info[key] = val;
        }
        sqlite3_finalize(stmt);
    }

    std::string dbDir =
        fcitx::StandardPath::global().userDirectory(fcitx::StandardPath::Type::Data) +
        "/fcitx5-lekhika";
    std::string fullPath = dbDir + "/lekhikadict.akshardb";

    const char *homeEnv = getenv("HOME");
    if (homeEnv && fullPath.find(homeEnv) == 0) {
        std::string shortened = "~" + fullPath.substr(std::strlen(homeEnv));
        info["db_path"] = shortened;
    } else {
        info["db_path"] = fullPath;
    }

    if (info.count("engine")) {
        info["db_name"] = info["engine"];
    }
    if (!info.count("format_version")) {
        info["format_version"] = "unknown";
    }
    if (!info.count("created_at")) {
        info["created_at"] = "unknown";
    }

    return info;
}


std::vector<std::string> DictionaryManager::findWords(const std::string &input, int limit) {
    std::vector<std::string> results;
    if (!db_ || input.empty()) return results;

    sqlite3_stmt *stmt = nullptr;

    //  Exact match
    const char *sqlExact = "SELECT word FROM words WHERE word = ? ORDER BY frequency DESC;";
    if (sqlite3_prepare_v2(db_, sqlExact, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, input.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            results.emplace_back(txt);
        }
        sqlite3_finalize(stmt);
    }

    if ((int)results.size() >= limit) return results;

    // Prefix match
    const char *sqlPrefix = "SELECT word FROM words WHERE word LIKE ? ORDER BY frequency DESC LIMIT ?;";
    if (sqlite3_prepare_v2(db_, sqlPrefix, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = input + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit - results.size());
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *txt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            results.emplace_back(txt);
        }
        sqlite3_finalize(stmt);
    }

    return results;
}

int DictionaryManager::getWordFrequency(const std::string &word) {
    if (!db_) return -1;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT frequency FROM words WHERE word = ?;";
    int frequency = -1;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return frequency;
    }

    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        frequency = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return frequency;
}

std::vector<std::pair<std::string, int>>
DictionaryManager::getAllWords(int limit, int offset, SortColumn sortBy, bool ascending) {
    std::vector<std::pair<std::string, int>> results;
    if (!db_) return results;
    sqlite3_stmt *stmt;

    std::string sql_str = "SELECT word, frequency FROM words ORDER BY ";
    if (sortBy == ByFrequency) {
        sql_str += "frequency ";
    } else { // Default to word
        sql_str += "word ";
    }

    if (ascending) {
        sql_str += "ASC";
    } else {
        sql_str += "DESC";
    }

    if (limit > 0) sql_str += " LIMIT ?";
    if (offset > 0) sql_str += " OFFSET ?";
    sql_str += ";";

    if (sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
        return results;
    }

    int bind_idx = 1;
    if (limit > 0) sqlite3_bind_int(stmt, bind_idx++, limit);
    if (offset > 0) sqlite3_bind_int(stmt, bind_idx++, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.emplace_back((const char *)sqlite3_column_text(stmt, 0),
                             sqlite3_column_int(stmt, 1));
    }

    sqlite3_finalize(stmt);
    return results;
}

void DictionaryManager::addWord(const std::string &word) {
    if (!db_) return;
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO words (word) VALUES (?) "
                      "ON CONFLICT(word) DO UPDATE SET frequency = frequency + 1;";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void DictionaryManager::removeWord(const std::string &word) {
    if (!db_) return;
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM words WHERE word = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<std::pair<std::string, int>>
DictionaryManager::searchWords(const std::string& searchTerm) {
    std::vector<std::pair<std::string, int>> results;
    if (!db_ || searchTerm.empty()) return results;
    sqlite3_stmt *stmt;

    // Use LIKE with wildcards to search for the term anywhere in the word
    std::string sql_str = "SELECT word, frequency FROM words WHERE word LIKE ? ORDER BY frequency DESC;";

    if (sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
        return results;
    }

    std::string pattern = "%" + searchTerm + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.emplace_back((const char *)sqlite3_column_text(stmt, 0),
                             sqlite3_column_int(stmt, 1));
    }

    sqlite3_finalize(stmt);
    return results;
}

bool DictionaryManager::updateWordFrequency(const std::string &word,
                                            int frequency) {
    if (!db_) return false;
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE words SET frequency = ? WHERE word = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, frequency);
    sqlite3_bind_text(stmt, 2, word.c_str(), -1, SQLITE_TRANSIENT);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success && (sqlite3_changes(db_) > 0);
}

// For large data manipulation in database
void DictionaryManager::beginTransaction() {
    char *zErrMsg = 0;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", NULL, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::string error = "SQL error: " + std::string(zErrMsg);
        sqlite3_free(zErrMsg);
        throw std::runtime_error(error);
    }
}

void DictionaryManager::commitTransaction() {
    char *zErrMsg = 0;
    int rc = sqlite3_exec(db_, "COMMIT;", NULL, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::string error = "SQL error: " + std::string(zErrMsg);
        sqlite3_free(zErrMsg);
        throw std::runtime_error(error);
    }
}

void DictionaryManager::rollbackTransaction() {
    // This is often called in a catch block, so we don't want it to throw.
    sqlite3_exec(db_, "ROLLBACK;", NULL, 0, NULL);
}

#endif


  //=============================================================================//
 // Transliteration Implementation                                              //
//=============================================================================//
Transliteration::Transliteration() {
    loadSpecialWords();
    loadMappings();
}

std::string Transliteration::readFileContentFromPackage(const std::string &filename) {
    auto path = fcitx::StandardPath::global().locate(fcitx::StandardPath::Type::PkgData, "fcitx5-lekhika/" + filename);
    if (path.empty()) {
        std::cerr << "Could not locate: " << filename << std::endl;
        return {};
    }
    return readFileContent(path);
}

void Transliteration::loadSpecialWords() {
    if (!enableAutoCorrect_)
        return;
    std::string content = readFileContentFromPackage("autocorrect.toml");
    if (!content.empty()) {
        parseSpecialWordsToml(content);
    }
}

void Transliteration::loadMappings() {
    std::string content = readFileContentFromPackage("mapping.toml");
    if (!content.empty()) {
        parseMappingsToml(content);
    }
}


std::string Transliteration::readFileContent(const std::string &relPath) {
    auto file = fcitx::StandardPath::global().open(fcitx::StandardPath::Type::PkgData, relPath,
                                                   O_RDONLY);
    if (file.fd() < 0) {
        return "";
    }
    std::string content;
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = fcitx::fs::safeRead(file.fd(), buffer, sizeof(buffer))) > 0) {
        content.append(buffer, bytesRead);
    }
    return content;
}

void Transliteration::parseSpecialWordsToml(const std::string &content) {
    std::istringstream iss(content);
    std::string line, section;
    while (std::getline(iss, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        if (section != "specialWords")
            continue;
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos)
            continue;
        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        specialWords_[key] = value;
    }
}

void Transliteration::parseMappingsToml(const std::string &content) {
    std::istringstream iss(content);
    std::string line, section;
    std::unordered_map<std::string, std::string> consonantMap;
    auto unquote = [](std::string str) -> std::string {
        if (str.size() >= 2 && ((str.front() == '"' && str.back() == '"') ||
                                (str.front() == '\'' && str.back() == '\''))) {
            str = str.substr(1, str.size() - 2);
        }
        std::string result;
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '\\' && i + 1 < str.size()) {
                char next = str[i + 1];
                if (next == '\\')
                    result += '\\';
                else if (next == 'n')
                    result += '\n';
                else if (next == 't')
                    result += '\t';
                else
                    result += next;
                ++i;
            } else {
                result += str[i];
            }
        }
        return result;
    };
    while (std::getline(iss, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos)
            continue;
        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        size_t commentPos = value.find('#');
        if (commentPos != std::string::npos) {
            value = value.substr(0, commentPos);
        }
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        key = unquote(key);
        value = unquote(value);
        if (section == "charMap") {
            charMap_[key] = value;
        } else if (section == "consonantMap") {
            consonantMap[key] = value;
        }
    }
    for (const auto &[conso, val] : consonantMap) {
        std::string consoMinusA = (conso.size() > 1 && conso.back() == 'a')
        ? conso.substr(0, conso.size() - 1)
        : conso;
        if (!charMap_.count(conso))
            charMap_[conso] = val;
        if (!charMap_.count(conso + "a"))
            charMap_[conso + "a"] = val + "ा";
        if (!charMap_.count(consoMinusA + "i"))
            charMap_[consoMinusA + "i"] = val + "ि";
        if (!charMap_.count(consoMinusA + "ee"))
            charMap_[consoMinusA + "ee"] = val + "ी";
        if (!charMap_.count(consoMinusA + "u"))
            charMap_[consoMinusA + "u"] = val + "ु";
        if (!charMap_.count(consoMinusA + "oo"))
            charMap_[consoMinusA + "oo"] = val + "ू";
        if (!charMap_.count(consoMinusA + "rri"))
            charMap_[consoMinusA + "rri"] = val + "ृ";
        if (!charMap_.count(consoMinusA + "e"))
            charMap_[consoMinusA + "e"] = val + "े";
        if (!charMap_.count(consoMinusA + "ai"))
            charMap_[consoMinusA + "ai"] = val + "ै";
        if (!charMap_.count(consoMinusA + "o"))
            charMap_[consoMinusA + "o"] = val + "ो";
        if (!charMap_.count(consoMinusA + "au"))
            charMap_[consoMinusA + "au"] = val + "ौ";
        if (!charMap_.count(consoMinusA))
            charMap_[consoMinusA] = val + "्";
    }
}

std::string
Transliteration::applyAutoCorrection(const std::string &word) const {
    auto it = specialWords_.find(word);
    if (it != specialWords_.end()) {
        return it->second;
    }
    return word;
}

bool Transliteration::isVowel(char c) const {
    return std::string("aeiou").find(tolower(c)) != std::string::npos;
}

std::string
Transliteration::applySmartCorrection(const std::string &input) const {
    std::string word = input;
    if (word.length() > 3) {
        char ec_0 = tolower(word.back());
        char ec_1 = tolower(word[word.length() - 2]);
        char ec_2 = tolower(word[word.length() - 3]);
        char ec_3 = word.length() > 3 ? tolower(word[word.length() - 4]) : '\0';
        if (!isVowel(ec_0) && ec_0 == 'y') {
            word = word.substr(0, word.length() - 1) + "ee";
        } else if (!(ec_0 == 'a' && ec_1 == 'h' && ec_2 == 'h') &&
                   !(ec_0 == 'a' && ec_1 == 'n' &&
                     (ec_2 == 'k' || ec_2 == 'h' || ec_2 == 'r')) &&
                   !(ec_0 == 'a' && ec_1 == 'r' &&
                     ((ec_2 == 'd' && ec_3 == 'n') ||
                      (ec_2 == 't' && ec_3 == 'n')))) {
            if (ec_0 == 'a' && (ec_1 == 'm' || (!isVowel(ec_1) && !isVowel(ec_3) &&
                                                ec_1 != 'y' && ec_2 != 'e'))) {
                word += "a";
            }
        }
        if (ec_0 == 'i' && !isVowel(ec_1) && !(ec_1 == 'r' && ec_2 == 'r')) {
            word = word.substr(0, word.length() - 1) + "ee";
        }
    }
    for (size_t i = 0; i < word.length(); ++i) {
        if (tolower(word[i]) == 'n' && i > 0 && i + 1 < word.length()) {
            char next_char = tolower(word[i + 1]);
            if (next_char == 'k' || next_char == 'g') {
                word.replace(i, 1, "ng");
                i++;
            }
        }
    }

    // Future use: Anusvara handling for specific consonants following 'm'
    /*
  const std::string anusvaraConsonants = "yrlvsh";
  for (size_t i = 0; i < word.length(); ++i) {
      if (tolower(word[i]) == 'm' && i + 1 < word.length()) {
          if (anusvaraConsonants.find(tolower(word[i + 1])) != std::string::npos) {
              // This logic would replace 'm' with an anusvara character, e.g., '*'
              // word[i] = '*';
          }
      }
  }
  */

    size_t pos_ng = word.find("ng");
    while (pos_ng != std::string::npos) {
        if (pos_ng >= 2 && pos_ng + 2 < word.length() &&
            isVowel(word[pos_ng + 2])) {
            word.replace(pos_ng, 2, "ngg");
            pos_ng = word.find("ng", pos_ng + 3);
        } else {
            pos_ng = word.find("ng", pos_ng + 1);
        }
    }
    for (size_t i = 0; i < word.size(); ++i) {
        if (word[i] == 'n' && i + 1 < word.size()) {
            char next = word[i + 1];
            if (next == 'T' || next == 'D') {
                word.replace(i, 1, "N");
                i++;
            } else if (next == 'c' && i + 2 < word.size() && word[i + 2] == 'h') {
                if (!(i + 3 < word.size() && word[i + 3] == 'h')) {
                    word.replace(i, 1, "ञ्");
                    i++;
                }
            }
        }
    }
    return word;
}

std::string Transliteration::preprocess(const std::string &input) {
    std::string processedWord = input;
    if (enableAutoCorrect_) {
        std::string autoCorrected = applyAutoCorrection(processedWord);
        if (autoCorrected != processedWord) {
            return autoCorrected;
        }
    }
    if (enableSmartCorrection_) {
        processedWord = applySmartCorrection(processedWord);
    }
    return processedWord;
}

std::string Transliteration::preprocessInput(const std::string &input) {
    std::string out;
    out.reserve(input.size());
    const std::string specialSymbols = "*";
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        std::string symbol(1, c);
        if (specialSymbols.find(c) != std::string::npos) {
            out += c;
            continue;
        }
        if (i > 0 && (c == '.' || c == '?' || charMap_.count(symbol)) &&
            !std::isalnum(static_cast<unsigned char>(c)) && input[i - 1] != ' ') {
            out += ' ';
        }
        out += c;
    }
    return out;
}

std::string Transliteration::transliterate(const std::string &input) {
    std::string preprocessed = preprocessInput(input);
    std::unordered_map<std::string, std::string> engTokens;
    std::string processed = preprocessed;
    size_t tokenCount = 1;
    size_t beginIndex = 0;
    size_t endIndex = 0;
    while ((beginIndex = processed.find("{", endIndex)) != std::string::npos) {
        endIndex = processed.find("}", beginIndex + 1);
        if (endIndex == std::string::npos) {
            endIndex = processed.size() - 1;
        }
        std::string token =
            processed.substr(beginIndex, endIndex - beginIndex + 1);
        std::string mask = "$-" + std::to_string(tokenCount++) + "-$";
        engTokens[mask] = token.substr(1, token.length() - 2);
        processed.replace(beginIndex, token.length(), mask);
        endIndex = beginIndex + mask.length();
    }
    std::string result;
    std::istringstream iss(processed);
    std::string segment;
    bool first = true;
    while (std::getline(iss, segment, ' ')) {
        if (!segment.empty()) {
            if (!first)
                result += " ";
            if (segment.length() == 1 && std::isdigit(segment[0]) &&
                !enableIndicNumbers_) {
                result += segment;
            } else if (segment.length() == 1 && !std::isalnum(segment[0]) &&
                       !enableSymbolsTransliteration_) {
                result += segment;
            } else if (segment.length() == 1 && charMap_.count(segment)) {
                result += charMap_[segment];
            } else {
                std::string cleaned = preprocess(segment);
                result += transliterateSegment(cleaned);
            }
            first = false;
        }
    }
    for (const auto &[mask, original] : engTokens) {
        std::string translatedMask = transliterateSegment(mask);
        size_t pos = 0;
        while ((pos = result.find(translatedMask, pos)) != std::string::npos) {
            result.replace(pos, translatedMask.length(), original);
            pos += original.length();
        }
    }
    return result;
}

std::string
Transliteration::transliterateSegment(const std::string &input) {
    std::string result;
    std::istringstream splitter(input);
    std::string subSegment;
    while (std::getline(splitter, subSegment, '/')) {
        if (!subSegment.empty()) {
            std::string subResult;
            std::string rem = subSegment;
            while (!rem.empty()) {
                std::string matched;
                for (int i = static_cast<int>(rem.size()); i > 0; --i) {
                    std::string part = rem.substr(0, i);
                    if (part.length() == 1 && std::isdigit(part[0]) &&
                        !enableIndicNumbers_) {
                        matched = part;
                        rem.erase(0, i);
                        break;
                    }
                    if (part.length() == 1 && !std::isalnum(part[0]) &&
                        !enableSymbolsTransliteration_) {
                        matched = part;
                        rem.erase(0, i);
                        break;
                    }
                    if (charMap_.count(part)) {
                        matched = charMap_[part];
                        rem.erase(0, i);
                        break;
                    }
                }
                if (!matched.empty()) {
                    subResult += matched;
                } else {
                    std::string singleChar(1, rem[0]);
                    if (std::isdigit(rem[0]) && !enableIndicNumbers_) {
                        subResult += rem[0];
                    } else if (!std::isalnum(rem[0]) && !enableSymbolsTransliteration_) {
                        subResult += rem[0];
                    } else if (charMap_.count(singleChar)) {
                        subResult += charMap_[singleChar];
                    } else {
                        subResult += rem[0];
                    }
                    rem.erase(0, 1);
                }
            }
            bool originalEndsWithHalanta =
                (!subSegment.empty() && subSegment.back() == '\\');
            bool resultEndsWithHalanta =
                (subResult.size() >= 3 &&
                 static_cast<unsigned char>(subResult[subResult.size() - 3]) ==
                     0xE0 &&
                 static_cast<unsigned char>(subResult[subResult.size() - 2]) ==
                     0xA5 &&
                 static_cast<unsigned char>(subResult[subResult.size() - 1]) ==
                     0x8D);
            if (resultEndsWithHalanta && !originalEndsWithHalanta &&
                subSegment.size() > 1) {
                subResult.resize(subResult.size() - 3);
            }
            result += subResult;
        }
    }
    return result;
}

