// lekhika_addon.cpp

#include "lekhika-addon.h"

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <string>

#ifdef HAVE_SQLITE3
#include <sqlite3.h>
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <vector>

using namespace fcitx;

#ifdef HAVE_SQLITE3
  //=============================================================================//
 // LekhikaCandidateList Implementation                                         //
//=============================================================================//

const Text &LekhikaCandidateList::label(int idx) const {
    return (idx >= 0 && idx < static_cast<int>(labels_.size()))
    ? labels_[idx]
    : empty_;
}

const CandidateWord &LekhikaCandidateList::candidate(int idx) const {
    static std::shared_ptr<CandidateWord> nullWord;
    return (idx >= 0 && idx < static_cast<int>(words_.size()))
               ? *words_[idx]
               : *nullWord;
}

void LekhikaCandidateList::append(std::unique_ptr<CandidateWord> word) {
    words_.emplace_back(std::move(word));
    labels_.emplace_back(std::to_string(words_.size()));
}
#endif

  //=============================================================================//
 // NepaliRomanEngine Implementation                                            //
//=============================================================================//

NepaliRomanEngine::NepaliRomanEngine(Instance *instance)
    : instance_(instance),
    factory_([](InputContext &) { return new NepaliRomanState; }) {
    instance_->inputContextManager().registerProperty("nepaliRomanState",
                                                      &factory_);
#ifdef HAVE_SQLITE3
    dictionary_ = std::make_unique<DictionaryManager>();
#endif
    transliterator_ = std::make_unique<Transliteration>();
    ensureConfigExists();
    applyConfig();
}

const Configuration *NepaliRomanEngine::getConfig() const { return &config_; }

Configuration *NepaliRomanEngine::getMutableConfig() { return &config_; }

void NepaliRomanEngine::setConfig(const RawConfig &rawConfig) {
    config_.load(rawConfig);
    applyConfig();

    auto path =
        StandardPath::global().userDirectory(StandardPath::Type::PkgConfig);
    auto filePath = path + "/addon/fcitx5lekhika.conf";
    safeSaveAsIni(config_, filePath);
}

void NepaliRomanEngine::applyConfig() {
    enableSmartCorrection_ = config_.enableSmartCorrection.value();
    enableAutoCorrect_ = config_.enableAutoCorrect.value();
    enableIndicNumbers_ = config_.enableIndicNumbers.value();
    enableSymbolsTransliteration_ = config_.enableSymbolsTransliteration.value();
    spacecanCommitSuggestions_ = config_.spacecanCommitSuggestions.value();

#ifdef HAVE_SQLITE3
    enableDictionaryLearning_ = config_.enableDictionaryLearning.value();
    enableSuggestion_ = config_.enableSuggestion.value();
    suggestionLimit_ = config_.suggestionLimit.value();
    horizontalLayout_ = config_.horizontalLayout.value();
#endif

    transliterator_->setEnableSmartCorrection(enableSmartCorrection_);
    transliterator_->setEnableAutoCorrect(enableAutoCorrect_);
    transliterator_->setEnableIndicNumbers(enableIndicNumbers_);
    transliterator_->setEnableSymbolsTransliteration(
        enableSymbolsTransliteration_);
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
    applyConfig();
}

void NepaliRomanEngine::activate(const InputMethodEntry &, InputContextEvent &) {
    reloadConfig();
}

void NepaliRomanEngine::keyEvent(const InputMethodEntry &, KeyEvent &keyEvent) {
    auto *ic = keyEvent.inputContext();
    if (!ic || keyEvent.isRelease()) {
        return;
    }

    auto *state = ic->propertyFor(&factory_);
    auto candidateList = ic->inputPanel().candidateList();
    bool isCandidateListVisible = static_cast<bool>(candidateList);

    const auto &sym = keyEvent.key().sym();
    const auto &key = keyEvent.key();

    // Candidate selection logic
    if (isCandidateListVisible) {
        // Commit with Space if option enabled OR user navigated in candidates
        if (sym == FcitxKey_space &&
            (spacecanCommitSuggestions_ || state->navigatedInCandidates_)) {
            if (candidateList->cursorIndex() >= 0) {
                const auto &word =
                    candidateList->candidate(candidateList->cursorIndex())
                        .text()
                        .toString();
                ic->commitString(word + " ");
                resetState(state, ic);
                state->navigatedInCandidates_ = false; // reset after commit
                keyEvent.filterAndAccept(); //  Consume
                return;
            }
        }
        // Commit with Enter or number keys
        if (sym == FcitxKey_Return ||
            (key.isSimple() && sym >= FcitxKey_1 && sym <= FcitxKey_9)) {
            int index = (sym == FcitxKey_Return)
            ? candidateList->cursorIndex()
            : (sym - FcitxKey_1);
            if (index >= 0 && index < candidateList->size()) {
                const auto &word =
                    candidateList->candidate(index).text().toString();
                ic->commitString(word + " ");
                resetState(state, ic);
                keyEvent.filterAndAccept();
                return;
            }
        }
    }

    // Move cursor
    if (sym == FcitxKey_Left) {
        if (!state->buffer_.empty() && state->cursorPos_ > 0) {
            state->cursorPos_--;
            updatePreedit(ic);
            keyEvent.filterAndAccept();
        }
        return;
    }
    if (sym == FcitxKey_Right) {
        if (!state->buffer_.empty() &&
            state->cursorPos_ < state->buffer_.length()) {
            state->cursorPos_++;
            updatePreedit(ic);
            keyEvent.filterAndAccept();
        }
        return;
    }

    // Candidate list navigation — Up/Down only
    if (isCandidateListVisible) {
        auto candidateList = ic->inputPanel().candidateList();
        if (candidateList) {
            int currentIndex = candidateList->cursorIndex();
            int total = candidateList->size();

            if (sym == FcitxKey_Up) {
                int newIndex = (currentIndex - 1 + total) % total;
                static_cast<LekhikaCandidateList*>(candidateList.get())->setCursorIndex(newIndex);
                state->navigatedInCandidates_ = true;
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }

            if (sym == FcitxKey_Down) {
                int newIndex = (currentIndex + 1) % total;
                static_cast<LekhikaCandidateList*>(candidateList.get())->setCursorIndex(newIndex);
                state->navigatedInCandidates_ = true;
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }
        }
    }

    // Enter: commit candidate or transliterated buffer — only consume if we commit something
    if (sym == FcitxKey_Return) {
        bool committed = false;

        if (isCandidateListVisible) {
            auto candidateList = ic->inputPanel().candidateList();
            if (candidateList && candidateList->cursorIndex() >= 0) {
                const auto &word =
                    candidateList->candidate(candidateList->cursorIndex())
                        .text()
                        .toString();
                ic->commitString(word);
                resetState(state, ic);
                committed = true;
            }
        }

        // If no candidate committed, try buffer
        if (!committed && !state->buffer_.empty()) {
            std::string result = transliterator_->transliterate(state->buffer_);
            ic->commitString(result);
#ifdef HAVE_SQLITE3
            if (dictionary_ && enableDictionaryLearning_) {
                dictionary_->addWord(result);
            }
#endif
            resetState(state, ic);
            committed = true;
        }

        // Only consume Enter if we actually committed something
        if (committed) {
            keyEvent.filterAndAccept();
        }
        // If nothing committed, key is NOT consumed → reaches app
        return;
    }

    // Space: commit candidate if allowed, else commit buffer or insert space
    if (sym == FcitxKey_space) {
        if (isCandidateListVisible) {
            auto candidateList = ic->inputPanel().candidateList();
            if (candidateList &&
                (spacecanCommitSuggestions_ || state->navigatedInCandidates_) &&
                candidateList->cursorIndex() >= 0) {
                const auto &word =
                    candidateList->candidate(candidateList->cursorIndex())
                        .text()
                        .toString();
                ic->commitString(word + " ");
                resetState(state, ic);
                state->navigatedInCandidates_ = false;
                keyEvent.filterAndAccept(); // Consume
                return;
            }
        }
        // Fallback: commit buffer or insert space
        if (!state->buffer_.empty()) {
            std::string result = transliterator_->transliterate(state->buffer_);
            ic->commitString(result + " ");
#ifdef HAVE_SQLITE3
            if (dictionary_ && enableDictionaryLearning_) {
                dictionary_->addWord(result);
            }
#endif
            resetState(state, ic);
            //  Do NOT consume — let Space reach app
            return;
        } else {
            ic->commitString(" ");
            //  Do NOT consume — let Space reach app
            return;
        }
    }

    // Esc: commit raw buffer as-is (no transliteration) and reset
    if (sym == FcitxKey_Escape) {
        if (!state->buffer_.empty()) {
            ic->commitString(state->buffer_);
            resetState(state, ic);
        }
        keyEvent.filterAndAccept(); //  Consume — IME-specific action
        return;
    }

    // Backspace
    if (sym == FcitxKey_BackSpace) {
        if (!state->buffer_.empty() && state->cursorPos_ > 0) {
            state->buffer_.erase(state->cursorPos_ - 1, 1);
            state->cursorPos_--;
            updatePreedit(ic);
            keyEvent.filterAndAccept();
        }
        return;
    }

    // Normal character input
    if (key.isSimple()) {
        std::string chr = key.keySymToUTF8(fcitx::KeySym(sym));

        static const std::string commitSymbols =
            R"(!@#$%^()-_=+[]{};:'",.<>?|\\)";
        bool isCommitSymbol = commitSymbols.find(chr) != std::string::npos;
        bool isNumber = chr.length() == 1 && std::isdigit(chr[0]);

        if (chr == "/") {
            commitBuffer(state, ic);
            ic->commitString(enableSymbolsTransliteration_
                                 ? transliterator_->transliterate(chr)
                                 : chr);
            keyEvent.filterAndAccept();
            return;
        }

        if (isCommitSymbol || (isNumber && !isCandidateListVisible)) {
            commitBuffer(state, ic);
            std::string symbolResult = chr;
            if ((isNumber && enableIndicNumbers_) ||
                (isCommitSymbol && enableSymbolsTransliteration_)) {
                symbolResult = transliterator_->transliterate(chr);
            }
            ic->commitString(symbolResult);
            updatePreedit(ic);
            keyEvent.filterAndAccept();
            return;
        }

        state->buffer_.insert(state->cursorPos_, chr);
        state->cursorPos_ += chr.length();
        updatePreedit(ic);
        keyEvent.filterAndAccept();
    }
}

void NepaliRomanEngine::commitBuffer(NepaliRomanState *state, InputContext *ic) {
    if (!state->buffer_.empty()) {
        std::string result = transliterator_->transliterate(state->buffer_);
        ic->commitString(result);
#ifdef HAVE_SQLITE3
        if (dictionary_ && enableDictionaryLearning_) {
            dictionary_->addWord(result);
        }
#endif
        resetState(state, ic);
    }
}

void NepaliRomanEngine::commitRawBuffer(NepaliRomanState *state, InputContext *ic) {
    if (!state->buffer_.empty()) {
        ic->commitString(state->buffer_);
        resetState(state, ic);
    }
}

void NepaliRomanEngine::resetState(NepaliRomanState *state, InputContext *ic) {
    state->buffer_.clear();
    state->cursorPos_ = 0;
    state->navigatedInCandidates_ = false;
    updatePreedit(ic);
}

void NepaliRomanEngine::deactivate(const InputMethodEntry &,
                                   InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    commitRawBuffer(state, ic);
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
        std::string preview_full =
            transliterator_->transliterate(state->buffer_);
        std::string preview_before_cursor = transliterator_->transliterate(
            state->buffer_.substr(0, state->cursorPos_));
        size_t cursor_in_preview_bytes = preview_before_cursor.length();
        preedit.append(preview_full, TextFormatFlag::Underline);
        preedit.setCursor(cursor_in_preview_bytes);
        aux.append(state->buffer_);
    }

    ic->inputPanel().setClientPreedit(preedit);
    ic->inputPanel().setAuxUp(aux);

    updateCandidates(ic, ic->propertyFor(&factory_)->buffer_);
    ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void NepaliRomanEngine::updateCandidates(InputContext *ic,
                                         const std::string &buffer) {
    ic->inputPanel().setCandidateList(nullptr); // clear old list
#ifdef HAVE_SQLITE3
    if (!dictionary_ || buffer.empty() || !enableSuggestion_)
        return;

    std::string prefix = transliterator_->transliterate(buffer);
    int limit = std::max(1, suggestionLimit_);
    std::vector<std::string> words = dictionary_->findWords(prefix, limit);

    if (words.empty())
        return;

    auto cands = std::make_unique<LekhikaCandidateList>(horizontalLayout_);
    for (const auto &w : words) {
        std::string u8 = w;
        if (!utf8::validate(u8.begin(), u8.end()))
            continue;
        cands->append(
            std::make_unique<LekhikaCandidateWord>(Text(std::move(u8))));
    }

    ic->inputPanel().setCandidateList(std::move(cands));
#endif
}

  //=============================================================================//
 // Factory Registration                                                        //
//=============================================================================//

class NepaliRomanEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new NepaliRomanEngine(manager->instance());
    }
};

FCITX_ADDON_FACTORY(NepaliRomanEngineFactory);
