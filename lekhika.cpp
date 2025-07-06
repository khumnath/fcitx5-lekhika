#include "lekhika.h"

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace fcitx;

// Constructor
NepaliRomanEngine::NepaliRomanEngine(Instance *instance)
    : instance_(instance),
      factory_([this](InputContext &) { return new NepaliRomanState; }) {
  instance_->inputContextManager().registerProperty("nepaliRomanState",
                                                     &factory_);
  ensureConfigExists();
  reloadConfig();
  loadSpecialWords();
  loadMappings();
}

const Configuration *NepaliRomanEngine::getConfig() const { return &config_; }

Configuration *NepaliRomanEngine::getMutableConfig() { return &config_; }

void NepaliRomanEngine::setConfig(const RawConfig &rawConfig) {
  config_.load(rawConfig);
  enableSmartCorrection_ = config_.enableSmartCorrection.value();
  enableAutoCorrect_ = config_.enableAutoCorrect.value();
  enableIndicNumbers_ = config_.enableIndicNumbers.value();
  enableSymbolsTransliteration_ = config_.enableSymbolsTransliteration.value();
  safeSaveAsIni(config_, "addon/fcitx5lekhika.conf");
}

void NepaliRomanEngine::ensureConfigExists() {
  auto path =
      StandardPath::global().userDirectory(StandardPath::Type::PkgConfig);
  auto filePath = path + "/addon/fcitx5lekhika.conf";
  if (!fs::isreg(filePath)) {
    RawConfig defaultConfig;
    config_.save(defaultConfig);
    safeSaveAsIni(defaultConfig, filePath);
  }
}

void NepaliRomanEngine::reloadConfig() {
  auto path =
      StandardPath::global().userDirectory(StandardPath::Type::PkgConfig);
  auto filePath = path + "/addon/fcitx5lekhika.conf";
  RawConfig rawConfig;
  if (fs::isreg(filePath)) {
    readAsIni(rawConfig, filePath);
  }
  config_.load(rawConfig);
  enableSmartCorrection_ = config_.enableSmartCorrection.value();
  enableAutoCorrect_ = config_.enableAutoCorrect.value();
  enableIndicNumbers_ = config_.enableIndicNumbers.value();
  enableSymbolsTransliteration_ = config_.enableSymbolsTransliteration.value();
}

void NepaliRomanEngine::activate(const InputMethodEntry &,
                                 InputContextEvent &event) {
  reloadConfig();
}

void NepaliRomanEngine::keyEvent(const InputMethodEntry &, KeyEvent &keyEvent) {
  auto *ic = keyEvent.inputContext();
  if (!ic || keyEvent.isRelease()) {
    return;
  }

  auto *state = ic->propertyFor(&factory_);
  const auto &sym = keyEvent.key().sym();

  if (sym == FcitxKey_Left) {
    if (!state->buffer_.empty()) {
      if (state->cursorPos_ > 0) {
        state->cursorPos_--;
        updatePreedit(ic);
      }
      keyEvent.filterAndAccept();
    }
    return;
  }
  if (sym == FcitxKey_Right) {
    if (!state->buffer_.empty()) {
      if (state->cursorPos_ < state->buffer_.length()) {
        state->cursorPos_++;
        updatePreedit(ic);
      }
      keyEvent.filterAndAccept();
    }
    return;
  }

  if (sym == FcitxKey_space) {
    if (!state->buffer_.empty()) {
      std::string result = transliterate(state->buffer_);
      ic->commitString(result + " ");
      state->buffer_.clear();
      state->cursorPos_ = 0;
      updatePreedit(ic);
    } else {
      ic->commitString(" ");
    }
    keyEvent.filterAndAccept();
    return;
  }

  if (sym == FcitxKey_Return) {
    if (!state->buffer_.empty()) {
      std::string result = transliterate(state->buffer_);
      ic->commitString(result);
      state->buffer_.clear();
      state->cursorPos_ = 0;
      updatePreedit(ic);
      keyEvent.filterAndAccept();
    }
    return;
  }

  if (sym == FcitxKey_BackSpace) {
    if (!state->buffer_.empty() && state->cursorPos_ > 0) {
      state->buffer_.erase(state->cursorPos_ - 1, 1);
      state->cursorPos_--;
      updatePreedit(ic);
      keyEvent.filterAndAccept();
    }
    return;
  }

  if (keyEvent.key().isSimple()) {
    std::string chr = keyEvent.key().keySymToUTF8(fcitx::KeySym(sym));

    static const std::string commitSymbols = R"(!@#$%^()-_=+[]{};:'",.<>?|\\)";
    bool isCommitSymbol = commitSymbols.find(chr) != std::string::npos;
    bool isNumber = chr.length() == 1 && std::isdigit(chr[0]);

    if (chr == "/") {
      if (!state->buffer_.empty()) {
        std::string result = transliterate(state->buffer_);
        ic->commitString(result);
        state->buffer_.clear();
        state->cursorPos_ = 0;
        updatePreedit(ic);
      } else {
        std::string slashResult = enableSymbolsTransliteration_ 
                                    ? transliterate(chr) 
                                    : chr;
        ic->commitString(slashResult);
      }
      keyEvent.filterAndAccept();
      return;
    }

    if (isCommitSymbol || isNumber) {
      bool hadBuffer = !state->buffer_.empty();

      if (hadBuffer) {
        std::string result = transliterate(state->buffer_);
        ic->commitString(result);
        state->buffer_.clear();
        state->cursorPos_ = 0;
        updatePreedit(ic);
      }

      std::string symbolResult;
      if (isNumber && enableIndicNumbers_) {
        symbolResult = transliterate(chr);
      } else if (isCommitSymbol && enableSymbolsTransliteration_) {
        symbolResult = transliterate(chr);
      } else {
        symbolResult = chr;
      }

      if (chr == "." || chr == "?") {
        ic->commitString((hadBuffer ? " " : "") + symbolResult);
      } else {
        ic->commitString(symbolResult);
      }

      keyEvent.filterAndAccept();
      return;
    }

    state->buffer_.insert(state->cursorPos_, chr);
    state->cursorPos_ += chr.length();
    updatePreedit(ic);
    keyEvent.filterAndAccept();
  }
}

void NepaliRomanEngine::deactivate(const InputMethodEntry &,
                                   InputContextEvent &event) {
  auto *ic = event.inputContext();
  auto *state = ic->propertyFor(&factory_);

  if (!state->buffer_.empty()) {
    std::string result = transliterate(state->buffer_);
    ic->commitString(result);
  }

  state->buffer_.clear();
  state->cursorPos_ = 0;

  Text empty;
  ic->inputPanel().setClientPreedit(empty);
  ic->inputPanel().setAuxUp(empty);
  ic->updatePreedit();
  ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void NepaliRomanEngine::reset(const InputMethodEntry &entry,
                              InputContextEvent &event) {
  deactivate(entry, event);
}

void NepaliRomanEngine::updatePreedit(InputContext *ic) {
  auto *state = ic->propertyFor(&factory_);

  Text preedit;
  Text aux;

  if (!state->buffer_.empty()) {
    std::string preview_full = transliterate(state->buffer_);
    std::string preview_before_cursor =
        transliterate(state->buffer_.substr(0, state->cursorPos_));
    size_t cursor_in_preview = preview_before_cursor.length();

    preview_full.insert(cursor_in_preview, "|");

    preedit.append(preview_full, TextFormatFlag::Underline);
    ic->inputPanel().setClientPreedit(preedit);
    aux.append(state->buffer_);
    ic->inputPanel().setAuxUp(aux);
  } else {
    ic->inputPanel().setClientPreedit(preedit);
    aux.clear();
    ic->inputPanel().setAuxUp(aux);
  }
  ic->updatePreedit();
  ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

bool NepaliRomanEngine::isVowel(char c) const {
  return std::string("aeiou").find(tolower(c)) != std::string::npos;
}

void NepaliRomanEngine::loadSpecialWords() {
  if (!enableAutoCorrect_)
    return;
  std::string content =
      readFileContent("/usr/share/fcitx5/fcitx5-lekhika/autocorrect.toml");
  if (!content.empty()) {
    parseSpecialWordsToml(content);
  }
}

void NepaliRomanEngine::loadMappings() {
  std::string content =
      readFileContent("/usr/share/fcitx5/fcitx5-lekhika/mapping.toml");
  if (!content.empty()) {
    parseMappingsToml(content);
  }
}

std::string
NepaliRomanEngine::readFileContent(const std::string &relPath) {
  auto file = StandardPath::global().open(StandardPath::Type::PkgData, relPath,
                                           O_RDONLY);

  if (file.fd() < 0) {
    return "";
  }

  std::string content;
  char buffer[4096];
  ssize_t bytesRead;

  while ((bytesRead = fs::safeRead(file.fd(), buffer, sizeof(buffer))) > 0) {
    content.append(buffer, bytesRead);
  }
  return content;
}

void NepaliRomanEngine::parseSpecialWordsToml(const std::string &content) {
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

void NepaliRomanEngine::parseMappingsToml(const std::string &content) {
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

  // Add vowel symbols with consonantMap entries
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
NepaliRomanEngine::applyAutoCorrection(const std::string &word) const {
  auto it = specialWords_.find(word);
  if (it != specialWords_.end()) {
    return it->second;
  }
  return word;
}

std::string
NepaliRomanEngine::applySmartCorrection(const std::string &input) const {
  std::string word = input;

  // Word ending modifications
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

  // 'n' to 'ng' conversion
  for (size_t i = 0; i < word.length(); ++i) {
    if (tolower(word[i]) == 'n' && i > 0 && i + 1 < word.length()) {
      char next_char = tolower(word[i + 1]);
      if (next_char == 'k' || next_char == 'g') {
        word.replace(i, 1, "ng");
        i++;
      }
    }
  }

  // 'm' to '*' (anusvara) rule
  const std::string anusvaraConsonants = "yrlvsh";
  for (size_t i = 0; i < word.length(); ++i) {
    if (tolower(word[i]) == 'm' && i + 1 < word.length()) {
      if (anusvaraConsonants.find(tolower(word[i + 1])) != std::string::npos) {
        word[i] = '*';
      }
    }
  }

  //  'ng' to 'ngg' transformation
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

  // Advanced 'n' replacements
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

std::string NepaliRomanEngine::preprocess(const std::string &input) {
  std::string processedWord = input;

  // Apply auto-correction if enabled.
  if (enableAutoCorrect_) {
    std::string autoCorrected = applyAutoCorrection(processedWord);
    if (autoCorrected != processedWord) {
      return autoCorrected;
    }
  }

  // If the word was not auto-corrected, apply smart corrections if enabled.
  if (enableSmartCorrection_) {
    processedWord = applySmartCorrection(processedWord);
  }

  return processedWord;
}

std::string NepaliRomanEngine::preprocessInput(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  const std::string specialSymbols = "*"; // Non-space-breaking symbols

  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    std::string symbol(1, c);

    // Skip adding space before special symbols like "*"
    if (specialSymbols.find(c) != std::string::npos) {
      out += c;
      continue;
    }

    if (i > 0 &&
        (c == '.' || c == '?' || charMap_.count(symbol)) &&
        !std::isalnum(static_cast<unsigned char>(c)) &&
        input[i - 1] != ' ') {
      out += ' ';
    }
    out += c;
  }
  return out;
}

std::string NepaliRomanEngine::transliterate(const std::string &input) {
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
      
      // Skip transliteration for numbers if Indic numbers are disabled
      if (segment.length() == 1 && std::isdigit(segment[0]) && !enableIndicNumbers_) {
        result += segment;
      }
      // Skip transliteration for symbols if symbols transliteration is disabled
      else if (segment.length() == 1 && !std::isalnum(segment[0]) && !enableSymbolsTransliteration_) {
        result += segment;
      }
      else if (segment.length() == 1 && charMap_.count(segment)) {
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
NepaliRomanEngine::transliterateSegment(const std::string &input) {
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
          
          // Skip mapping for numbers if Indic numbers are disabled
          if (part.length() == 1 && std::isdigit(part[0]) && !enableIndicNumbers_) {
            matched = part;
            rem.erase(0, i);
            break;
          }
          
          // Skip mapping for symbols if symbols transliteration is disabled
          if (part.length() == 1 && !std::isalnum(part[0]) && !enableSymbolsTransliteration_) {
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
          // Skip transliteration for numbers if Indic numbers are disabled
          if (std::isdigit(rem[0]) && !enableIndicNumbers_) {
            subResult += rem[0];
          }
          // Skip transliteration for symbols if symbols transliteration is disabled
          else if (!std::isalnum(rem[0]) && !enableSymbolsTransliteration_) {
            subResult += rem[0];
          }
          else if (charMap_.count(singleChar)) {
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

// Addon Factory
class NepaliRomanEngineFactory : public AddonFactory {
public:
  AddonInstance *create(AddonManager *manager) override {
    return new NepaliRomanEngine(manager->instance());
  }
};

FCITX_ADDON_FACTORY(NepaliRomanEngineFactory);