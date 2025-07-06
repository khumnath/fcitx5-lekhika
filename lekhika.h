#ifndef LEKHIKA_H
#define LEKHIKA_H

#include <fcitx-config/configuration.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include <string>
#include <unordered_map>
#include <vector>

using namespace fcitx;

// Configuration options for the engine
FCITX_CONFIGURATION(
    NepaliRomanEngineConfig,
    Option<bool> enableSmartCorrection{this, "EnableSmartCorrection(Rule based)",
                                       "Enable Smart Correction", true};
    Option<bool> enableAutoCorrect{this, "EnableAutoCorrect",
                                   "Enable Auto-Correction(From dictionary)", true};
    Option<bool> enableIndicNumbers{this, "EnableIndicNumbers",
                                    "Enable Indic Numbers Transliteration", true};
    Option<bool> enableSymbolsTransliteration{this, "EnableSymbolsTransliteration",
                                              "Enable Symbols Transliteration", true};);

// State for each input context
class NepaliRomanState : public InputContextProperty {
public:
  std::string buffer_;
  size_t cursorPos_ = 0;
};

// The main input method engine class
class NepaliRomanEngine : public InputMethodEngine {
public:
  NepaliRomanEngine(Instance *instance);

  // Overridden methods from fcitx::InputMethodEngine
  const Configuration *getConfig() const override;
  Configuration *getMutableConfig();
  void setConfig(const RawConfig &rawConfig) override;
  void activate(const InputMethodEntry &, InputContextEvent &) override;
  void deactivate(const InputMethodEntry &, InputContextEvent &event) override;
  void keyEvent(const InputMethodEntry &, KeyEvent &keyEvent) override;
  void reset(const InputMethodEntry &entry, InputContextEvent &event) override;
  void reloadConfig() override;

private:
  // Private methods
  void ensureConfigExists();
  void updatePreedit(InputContext *ic);
  bool isVowel(char c) const;
  void loadSpecialWords();
  void loadMappings();
  std::string readFileContent(const std::string &relPath);
  void parseSpecialWordsToml(const std::string &content);
  void parseMappingsToml(const std::string &content);
  std::string applyAutoCorrection(const std::string &word) const;
  std::string applySmartCorrection(const std::string &input) const;
  std::string preprocess(const std::string &input);
  std::string preprocessInput(const std::string &input);
  std::string transliterate(const std::string &input);
  std::string transliterateSegment(const std::string &input);

  // Member variables
  Instance *instance_;
  FactoryFor<NepaliRomanState> factory_;
  std::unordered_map<std::string, std::string> charMap_;
  std::unordered_map<std::string, std::string> specialWords_;
  NepaliRomanEngineConfig config_;
  bool enableSmartCorrection_ = true;
  bool enableAutoCorrect_ = true;
  bool enableIndicNumbers_ = true;
  bool enableSymbolsTransliteration_ = true;
};

#endif // LEKHIKA_H