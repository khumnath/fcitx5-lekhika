#ifndef LEKHIKA_ADDON_H
#define LEKHIKA_ADDON_H

#include <fcitx-config/configuration.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/action.h>

#include <lekhika/lekhika_core.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace fcitx;

#ifdef HAVE_SQLITE3
/* ----------  concrete candidate-word object  ---------- */
class LekhikaCandidateWord : public CandidateWord {
public:
    explicit LekhikaCandidateWord(Text t)
        : CandidateWord(std::move(t)) {}

    void select(InputContext *ic) const override {
        ic->commitString(text().toString());
    }
};

/* ----------  custom candidate-list  ---------- */
class LekhikaCandidateList : public CandidateList {
public:
    explicit LekhikaCandidateList(bool horizontal = false)
        : horizontal_(horizontal) {}

    const Text &label(int idx) const override;
    const CandidateWord &candidate(int idx) const override;
    int size() const override { return static_cast<int>(words_.size()); }
    int cursorIndex() const override { return cursor_; }

    CandidateLayoutHint layoutHint() const override {
        return horizontal_ ? CandidateLayoutHint::Horizontal
                           : CandidateLayoutHint::Vertical;
    }

    void setCursorIndex(int c) { cursor_ = c; }
    void append(std::unique_ptr<CandidateWord> word);
    bool empty() const { return words_.empty(); }

private:
    std::vector<std::shared_ptr<CandidateWord>> words_;
    std::vector<Text> labels_;
    int cursor_ = 0;
    Text empty_;
    bool horizontal_ = false;
};
#endif // HAVE_SQLITE3

/* ----------  configuration  ---------- */
FCITX_CONFIGURATION(
    NepaliRomanEngineConfig,
    Option<bool> enableSmartCorrection{this, "EnableSmartCorrection", "Enable Smart Correction", true};
    Option<bool> enableAutoCorrect{this, "EnableAutoCorrect", "Enable Auto-Correction", true};
    Option<bool> enableIndicNumbers{this, "EnableIndicNumbers", "Enable Indic Numbers", true};
    Option<bool> enableSymbolsTransliteration{this, "EnableSymbolsTransliteration", "Enable Symbols", true};
    Option<bool> enableSuggestion{this, "EnableSuggestions", "Enable Suggestions", true};
    Option<bool> enableDictionaryLearning{this, "EnableDictionaryLearning", "Enable Dictionary Learning", false};
    Option<bool> horizontalLayout{this, "HorizontalLayout", "Display candidates horizontally", false};
    Option<int> suggestionLimit{this, "SuggestionLimit", "Maximum number of suggestions", 7};
    Option<bool> spacecanCommitSuggestions{this, "UseSpacetoCommitSuggestions", "Use Space to Commit Suggestions", false};
    );

/* ----------  per-input-context state  ---------- */
class NepaliRomanState : public InputContextProperty {
public:
    std::string buffer_;
    size_t cursorPos_ = 0;
    bool navigatedInCandidates_ = false;
};

/* ----------  main engine  ---------- */
class NepaliRomanEngine : public InputMethodEngine {
public:
    explicit NepaliRomanEngine(Instance *instance);

    const Configuration *getConfig() const override;
    Configuration *getMutableConfig();
    void setConfig(const RawConfig &rawConfig) override;
    void activate(const InputMethodEntry &, InputContextEvent &) override;
    void deactivate(const InputMethodEntry &, InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &, KeyEvent &keyEvent) override;
    void reset(const InputMethodEntry &entry, InputContextEvent &event) override;
    void reloadConfig() override;

private:
    // Non-virtual helpers
    void applyConfig();
    void ensureConfigExists();
    void updatePreedit(InputContext *ic);
    void updateCandidates(InputContext *ic, const std::string &prefix);
    void commitBuffer(NepaliRomanState *state, InputContext *ic);
    void commitRawBuffer(NepaliRomanState *state, InputContext *ic);
    void resetState(NepaliRomanState *state, InputContext *ic);

    Instance *instance_;
    FactoryFor<NepaliRomanState> factory_;
    std::unique_ptr<Transliteration> transliterator_;

#ifdef HAVE_SQLITE3
    std::unique_ptr<DictionaryManager> dictionary_;
    bool enableSuggestion_ = false;
    bool enableDictionaryLearning_ = false;
    int suggestionLimit_ = 7;
    bool horizontalLayout_ = false;
#endif

    NepaliRomanEngineConfig config_;
    bool enableSmartCorrection_ = true;
    bool enableAutoCorrect_ = true;
    bool enableIndicNumbers_ = true;
    bool enableSymbolsTransliteration_ = true;
    bool spacecanCommitSuggestions_ = false;
};

#endif // LEKHIKA_ADDON_H
