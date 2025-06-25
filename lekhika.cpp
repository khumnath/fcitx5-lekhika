#include <fcitx/inputmethodengine.h>
#include <fcitx/candidatelist.h>
#include <fcitx/action.h>
#include <fcitx/instance.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonmanager.h>
#include <fcitx/addonfactory.h>
#include <fcitx/inputpanel.h>
#include <fcitx/inputcontext.h>
#include <fcitx/text.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/fs.h>

#include <unordered_map>
#include <string>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

using namespace fcitx;

class NepaliRomanEngine : public InputMethodEngine {
public:
    NepaliRomanEngine(Instance *instance) : instance_(instance) {
        loadSpecialWords();   // Load from autocorrect.toml
        loadMappings();       // Load from mapping.toml
    }

    void keyEvent(const InputMethodEntry &, KeyEvent &keyEvent) override {
        auto *ic = keyEvent.inputContext();
        if (!ic || keyEvent.isRelease()) return;
    
        const auto &sym = keyEvent.key().sym();
    
        // Handle cursor movement keys
        if (sym == FcitxKey_Left) {
            if (cursorPos_ > 0) {
                cursorPos_--;
                updatePreedit(ic);
            }
            keyEvent.filterAndAccept();
            return;
        } else if (sym == FcitxKey_Right) {
            if (cursorPos_ < buffer_.length()) {
                cursorPos_++;
                updatePreedit(ic);
            }
            keyEvent.filterAndAccept();
            return;
        }
    
        // Space key
        if (sym == FcitxKey_space) {
            if (!buffer_.empty()) {
                std::string result = transliterate(buffer_);
                ic->commitString(result + " ");
                buffer_.clear();
                cursorPos_ = 0;
                updatePreedit(ic);
            } else {
                ic->commitString(" ");
            }
            keyEvent.filterAndAccept();
            return;
        }
    
        // Return key (commit buffer, but allow Enter to propagate if buffer was empty)
        if (sym == FcitxKey_Return) {
            if (!buffer_.empty()) {
                std::string result = transliterate(buffer_);
                ic->commitString(result);
                buffer_.clear();
                cursorPos_ = 0;
                updatePreedit(ic);
                keyEvent.filterAndAccept();
            }
            return;  // Do not accept if buffer was empty, let Enter pass through
        }
    
        // Backspace
        if (sym == FcitxKey_BackSpace) {
            if (!buffer_.empty() && cursorPos_ > 0) {
                buffer_.erase(cursorPos_ - 1, 1);
                cursorPos_--;
                updatePreedit(ic);
                keyEvent.filterAndAccept();
            }
            return;
        }
    
        // Handle normal keys
        if (keyEvent.key().isSimple()) {
            std::string chr = keyEvent.key().keySymToUTF8(fcitx::KeySym(sym));
            bool isSymbol = std::string(R"(!@#$%^&*()-_=+[]{};:'",.<>?/\\|)").find(chr) != std::string::npos;
            bool isNumber = chr.length() == 1 && std::isdigit(chr[0]);
    
            if (isSymbol || isNumber) {
                bool hadBuffer = !buffer_.empty();
    
                if (hadBuffer) {
                    std::string result = transliterate(buffer_);
                    ic->commitString(result);
                    buffer_.clear();
                    cursorPos_ = 0;
                    updatePreedit(ic);
                }
    
                // Commit symbol with space before "." or "?" if there was a buffer
                if (chr == "." || chr == "?") {
                    std::string symbolResult = transliterate(chr);
                    ic->commitString((hadBuffer ? " " : "") + symbolResult);
                } else {
                    std::string symbolResult = transliterate(chr);
                    ic->commitString(symbolResult);
                }
    
                keyEvent.filterAndAccept();
                return;
            }
    
            // Normal character insert
            buffer_.insert(cursorPos_, chr);
            cursorPos_ += chr.length();
            updatePreedit(ic);
            keyEvent.filterAndAccept();
        }
    }
    
private:
    Instance *instance_;
    std::string buffer_;
    std::unordered_map<std::string, std::string> charMap_;
    std::unordered_map<std::string, std::string> specialWords_;
    bool preeditStarted_ = false;
    size_t cursorPos_ = 0;

    void updatePreedit(InputContext *ic) {
        Text preedit, aux;

        if (!buffer_.empty()) {
            std::string preview = transliterate(buffer_);
            preedit.append(preview, TextFormatFlag::Underline);
            aux.append("");
            aux.append(buffer_);
        } else {
            // Clear aux text when the buffer is empty
            aux.clear();
        }

        // Set preedit and auxiliary text
        ic->inputPanel().setClientPreedit(preedit);
        ic->inputPanel().setAuxUp(aux);

        // Ensure full UI refresh
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    bool isVowel(char c) {
        return std::string("aeiou").find(tolower(c)) != std::string::npos;
    }

    void loadSpecialWords() {
        std::string content = readFileContent("/usr/share/fcitx5/fcitx5-lekhika/autocorrect.toml");
        if (!content.empty()) {
            parseSpecialWordsToml(content);
        }
    }

    void loadMappings() {
        std::string content = readFileContent("/usr/share/fcitx5/fcitx5-lekhika/mapping.toml");
        if (!content.empty()) {
            parseMappingsToml(content);
        }
    }

    std::string readFileContent(const std::string &relPath) {
        auto file = StandardPath::global().open(
            StandardPath::Type::PkgData, relPath, O_RDONLY);
        
        if (file.fd() < 0) return "";
    
        std::string content;
        char buffer[4096];
        ssize_t bytesRead;
    
        while ((bytesRead = fs::safeRead(file.fd(), buffer, sizeof(buffer))) > 0) {
            content.append(buffer, bytesRead);
        }
    
        return content;
    }
    

    void parseSpecialWordsToml(const std::string &content) {
        std::istringstream iss(content);
        std::string line, section;
        
        while (std::getline(iss, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);
            
            if (line.empty() || line[0] == '#') continue;
            
            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            
            if (section != "specialWords") continue;
            
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            
            // Trim key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Remove quotes
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            
            specialWords_[key] = value;
        }
    }

    void parseMappingsToml(const std::string &content) {
        std::istringstream iss(content);
        std::string line, section;
        std::unordered_map<std::string, std::string> consonantMap;
    
        auto unquote = [](std::string str) -> std::string {
            if (str.size() >= 2 && ((str.front() == '"' && str.back() == '"') || 
                                    (str.front() == '\'' && str.back() == '\''))) {
                str = str.substr(1, str.size() - 2);
            }
            // Handle escaped characters manually
            std::string result;
            for (size_t i = 0; i < str.size(); ++i) {
                if (str[i] == '\\' && i + 1 < str.size()) {
                    char next = str[i + 1];
                    if (next == '\\') result += '\\';
                    else if (next == 'n') result += '\n';
                    else if (next == 't') result += '\t';
                    else result += next;
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
    
            if (line.empty() || line[0] == '#') continue;
    
            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
    
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
    
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
    
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
    
            key = unquote(key);
            value = unquote(value);
    
            if (section == "charMap") {
                charMap_[key] = value;
                FCITX_INFO() << "[charMap] " << key << " = " << value;
            } else if (section == "consonantMap") {
                consonantMap[key] = value;
                FCITX_INFO() << "[consonantMap] " << key << " = " << value;
            }
        }
    
        // Generate derived forms for consonants
        for (const auto& [conso, val] : consonantMap) {
            std::string consoMinusA = (conso.size() > 1 && conso.back() == 'a') ? 
                conso.substr(0, conso.size() - 1) : conso;
    
            if (!charMap_.count(conso)) charMap_[conso] = val;
            if (!charMap_.count(conso + "a")) charMap_[conso + "a"] = val + "ा";
            if (!charMap_.count(consoMinusA + "i")) charMap_[consoMinusA + "i"] = val + "ि";
            if (!charMap_.count(consoMinusA + "ee")) charMap_[consoMinusA + "ee"] = val + "ी";
            if (!charMap_.count(consoMinusA + "u")) charMap_[consoMinusA + "u"] = val + "ु";
            if (!charMap_.count(consoMinusA + "oo")) charMap_[consoMinusA + "oo"] = val + "ू";
            if (!charMap_.count(consoMinusA + "ri")) charMap_[consoMinusA + "ri"] = val + "ृ";
            if (!charMap_.count(consoMinusA + "e")) charMap_[consoMinusA + "e"] = val + "े";
            if (!charMap_.count(consoMinusA + "ai")) charMap_[consoMinusA + "ai"] = val + "ै";
            if (!charMap_.count(consoMinusA + "o")) charMap_[consoMinusA + "o"] = val + "ो";
            if (!charMap_.count(consoMinusA + "au")) charMap_[consoMinusA + "au"] = val + "ौ";
            if (!charMap_.count(consoMinusA)) charMap_[consoMinusA] = val + "्";
        }
    }
    
    std::string preprocess(const std::string &input) {
        // First check if it's a special word that needs direct replacement
        auto it = specialWords_.find(input);
        if (it != specialWords_.end()) return it->second;

        std::string word = input;

        // Special handling for words ending in 'cha', 'chu', etc.
        if (word.length() >= 3) {
            std::string last3 = word.substr(word.length() - 3);
            if (last3 == "cha") word = word.substr(0, word.length() - 3) + "chha";
            else if (last3 == "chu") word = word.substr(0, word.length() - 3) + "chhu";
        }

        // Replace ri^ with ari^
        size_t pos = 0;
        while ((pos = word.find("ri^", pos)) != std::string::npos) {
            word.replace(pos, 3, "ari^");
            pos += 4;
        }

        if (word.length() > 3) {
            char ec_0 = tolower(word.back());
            char ec_1 = tolower(word[word.length() - 2]);
            char ec_2 = tolower(word[word.length() - 3]);
            char ec_3 = word.length() > 3 ? tolower(word[word.length() - 4]) : '\0';

            // Handle "cha", "chu", "che" endings
            if ((ec_0 == 'a' || ec_0 == 'e' || ec_0 == 'u') && ec_1 == 'h' && ec_2 == 'c') {
                word = word.substr(0, word.length() - 3) + "chh" + ec_0;
            }
            // Handle y-ending conversion to ee
            else if (ec_0 == 'y') {
                word = word.substr(0, word.length() - 1) + "ee";
            }
            // Complex word-ending rules
            else if (!(ec_0 == 'a' && ec_1 == 'h' && ec_2 == 'h') &&
                     !(ec_0 == 'a' && ec_1 == 'n' && (ec_2 == 'k' || ec_2 == 'h' || ec_2 == 'r')) &&
                     !(ec_0 == 'a' && ec_1 == 'r' && ((ec_2 == 'd' && ec_3 == 'n') || (ec_2 == 't' && ec_3 == 'n')))) {
                
                if (ec_0 == 'a' && (ec_1 == 'm' || (!isVowel(ec_1) && !isVowel(ec_3) && ec_1 != 'y' && ec_2 != 'e'))) {
                    word += "a";
                }
            }

            // Convert i to ee after consonants
            if (ec_0 == 'i' && !isVowel(ec_1)) {
                word = word.substr(0, word.length() - 1) + "ee";
            }
        }

        return word;
    }

    std::string preprocessInput(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        // Add spaces before punctuation
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            std::string symbol(1, c);
            
            // Add space before symbols that are in charMap
            if (i > 0 && (c == '.' || c == '?' || charMap_.count(symbol)) && 
                !std::isalnum(static_cast<unsigned char>(c)) && 
                input[i-1] != ' ') {
                out += ' ';
            }
            out += c;
        }
        return out;
    }

    std::string transliterate(const std::string &input) {
        // Handle token replacement
        std::string preprocessed = preprocessInput(input);
        
        // Handle {token} preservation
        std::unordered_map<std::string, std::string> engTokens;
        std::string processed = preprocessed;
        size_t tokenCount = 1;
        
        // Extract {tokens}
        size_t beginIndex = 0;
        size_t endIndex = 0;
        while ((beginIndex = processed.find("{", endIndex)) != std::string::npos) {
            endIndex = processed.find("}", beginIndex + 1);
            if (endIndex == std::string::npos) endIndex = processed.size() - 1;
            
            std::string token = processed.substr(beginIndex, endIndex - beginIndex + 1);
            std::string mask = "$-" + std::to_string(tokenCount++) + "-$";
            engTokens[mask] = token.substr(1, token.length() - 2);
            
            // Replace token with mask
            processed.replace(beginIndex, token.length(), mask);
            endIndex = beginIndex + mask.length();
        }

        // Process the text
        std::string result;
        std::istringstream iss(processed);
        std::string segment;
        bool first = true;
        
        while (std::getline(iss, segment, ' ')) {
            if (!segment.empty()) {
                if (!first) result += " ";
                if (segment.length() == 1 && charMap_.count(segment)) {
                    result += charMap_[segment];
                } else {
                    std::string cleaned = preprocess(segment);
                    result += transliterateSegment(cleaned);
                }
                first = false;
            }
        }

        // Restore tokens
        for (const auto& [mask, original] : engTokens) {
            std::string translatedMask = transliterateSegment(mask);
            size_t pos = 0;
            while ((pos = result.find(translatedMask, pos)) != std::string::npos) {
                result.replace(pos, translatedMask.length(), original);
                pos += original.length();
            }
        }

        return result;
    }

    std::string transliterateSegment(const std::string &input) {
        // Handle segments split by '/'
        std::string result;
        std::istringstream splitter(input);
        std::string subSegment;
        
        while (std::getline(splitter, subSegment, '/')) {
            if (!subSegment.empty()) {
                std::string subResult;
                std::string rem = subSegment;
                
                // Process each subsegment
                while (!rem.empty()) {
                    std::string matched;
                    for (int i = static_cast<int>(rem.size()); i > 0; --i) {
                        std::string part = rem.substr(0, i);
                        if (charMap_.count(part)) {
                            matched = charMap_[part];
                            rem.erase(0, i);
                            break;
                        }
                    }
                    if (!matched.empty()) {
                        subResult += matched;
                    } else {
                        // Try matching as a single character
                        std::string singleChar(1, rem[0]);
                        if (charMap_.count(singleChar)) {
                            subResult += charMap_[singleChar];
                        } else {
                            subResult += rem[0];
                        }
                        rem.erase(0, 1);
                    }
                }

                // Remove trailing halanta unless explicitly typed
                bool originalEndsWithHalanta = (!subSegment.empty() && subSegment.back() == '\\');
                bool resultEndsWithHalanta = (subResult.size() >= 3 &&
                    static_cast<unsigned char>(subResult[subResult.size() - 3]) == 0xE0 &&
                    static_cast<unsigned char>(subResult[subResult.size() - 2]) == 0xA5 &&
                    static_cast<unsigned char>(subResult[subResult.size() - 1]) == 0x8D);
                if (resultEndsWithHalanta && !originalEndsWithHalanta && subSegment.size() > 1) {
                    subResult.resize(subResult.size() - 3);
                }

                result += subResult;
            }
        }

        return result;
    }
};

class NepaliRomanEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new NepaliRomanEngine(manager->instance());
    }
};

FCITX_ADDON_FACTORY(NepaliRomanEngineFactory);