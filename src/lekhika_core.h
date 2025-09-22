/********************************************************************
 * lekhika-core.h  –  lekhika core header
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
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <map>

#ifdef HAVE_SQLITE3
struct sqlite3;

class DictionaryManager {
public:
    DictionaryManager();
    ~DictionaryManager();
    void reset();
    std::map<std::string, std::string> getDatabaseInfo();
    void addWord(const std::string &word);
    void removeWord(const std::string &word);
    std::vector<std::string> findWords(const std::string &prefix, int limit);
    int getWordFrequency(const std::string &word);
    bool updateWordFrequency(const std::string &word, int frequency);

    enum SortColumn { ByWord = 0, ByFrequency = 1 };
    std::vector<std::pair<std::string, int>> getAllWords(int limit = -1, int offset = 0, SortColumn sortBy = ByWord, bool ascending = true);
    std::vector<std::pair<std::string, int>> searchWords(const std::string& searchTerm);

    // Transaction management
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();

private:
    void initializeDatabase();
    sqlite3 *db_;
};
#endif


class Transliteration {
public:
    Transliteration();

    std::string transliterate(const std::string &input);

    void setEnableSmartCorrection(bool enable) { enableSmartCorrection_ = enable; }
    void setEnableAutoCorrect(bool enable) { enableAutoCorrect_ = enable; }
    void setEnableIndicNumbers(bool enable) { enableIndicNumbers_ = enable; }
    void setEnableSymbolsTransliteration(bool enable) { enableSymbolsTransliteration_ = enable; }

private:
    void loadMappings();
    void loadSpecialWords();
    void parseMappingsToml(const std::string &content);
    void parseSpecialWordsToml(const std::string &content);
    std::string readFileContent(const std::string &path);
    std::string readFileContentFromPackage(const std::string &path);
    std::string transliterateSegment(const std::string &segment);
    std::string preprocess(const std::string &word);
    std::string applySmartCorrection(const std::string &word) const;
    std::string applyAutoCorrection(const std::string &word) const;
    std::string preprocessInput(const std::string &input);
    bool isVowel(char c) const;

    std::unordered_map<std::string, std::string> charMap_;
    std::unordered_map<std::string, std::string> specialWords_;

    bool enableSmartCorrection_ = true;
    bool enableAutoCorrect_ = true;
    bool enableIndicNumbers_ = true;
    bool enableSymbolsTransliteration_ = true;
};

