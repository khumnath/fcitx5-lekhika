//lekhika_core.h

#ifndef LEKHIKA_CORE_H
#define LEKHIKA_CORE_H

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <map>

#ifdef HAVE_SQLITE3
struct sqlite3;

class DictionaryManager {
public:
    DictionaryManager();
    ~DictionaryManager();

    std::vector<std::string> findWords(const std::string &prefix, int limit);
    int getWordFrequency(const std::string &word);
    std::vector<std::pair<std::string, int>> getAllWords(int limit = -1, int offset = 0);
    std::map<std::string, std::string> getDatabaseInfo();

    void addWord(const std::string &word);
    void removeWord(const std::string &word);
    bool updateWordFrequency(const std::string &word, int frequency);
    void reset();

    static std::string getDatabasePath();

private:
    void initializeDatabase();
    sqlite3 *db_;
};

bool isValidDevanagariWord(const std::string &utf8);
#endif

class Transliteration {
public:
    Transliteration();
    std::string transliterate(const std::string &input);

    void setEnableSmartCorrection(bool enabled) { enableSmartCorrection_ = enabled; }
    void setEnableAutoCorrect(bool enabled) { enableAutoCorrect_ = enabled; }
    void setEnableIndicNumbers(bool enabled) { enableIndicNumbers_ = enabled; }
    void setEnableSymbolsTransliteration(bool enabled) { enableSymbolsTransliteration_ = enabled; }

    bool enableSmartCorrection_ = true;
    bool enableAutoCorrect_ = true;
    bool enableIndicNumbers_ = true;
    bool enableSymbolsTransliteration_ = true;

private:
    void loadMappings();
    void loadSpecialWords();
    std::string readFileContent(const std::string &relPath);
    std::string readFileContentFromPackage(const std::string &filename);
    void parseMappingsToml(const std::string &content);
    void parseSpecialWordsToml(const std::string &content);
    bool isVowel(char c) const;
    std::string applyAutoCorrection(const std::string &word) const;
    std::string applySmartCorrection(const std::string &input) const;
    std::string preprocess(const std::string &input);
    std::string preprocessInput(const std::string &input);
    std::string transliterateSegment(const std::string &input);

    std::unordered_map<std::string, std::string> charMap_;
    std::unordered_map<std::string, std::string> specialWords_;
};

#endif // LEKHIKA_CORE_H

