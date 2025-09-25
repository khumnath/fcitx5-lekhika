![Downloads](https://img.shields.io/github/downloads/khumnath/fcitx5-lekhika/dictionary/lekhikadict.akshardb.svg)

> [!NOTE]  
>
> We need a valid nepali word list file with many words per line for creating good database.
> we can train with any text file but function to filter only valid words is not complete yet.
> if anyone wants to contribute create a file wordlist.text and send a pull request.

# fcitx5-lekhika

**fcitx5-lekhika** is a transliteration-based input method for [Fcitx5](https://github.com/fcitx/fcitx5) focused on improving Nepali typing workflows. The project has a modular design, using the core transliteration engine from the [liblekhika](https://github.com/khumnath/liblekhika) repository as a Git submodule.

It consists of two main components for the end-user:

1. **Fcitx5 Module (`fcitx5-lekhika`):** A phonetic/rule-based input method engine that integrates system-wide for seamless Nepali typing.

2. **Dictionary Trainer (`lekhika-trainer`):** A graphical (Qt6) tool to manage the dictionary, learn new words from text files, and test transliteration settings.

## ✨ Features

* **Core Engine (Fcitx5 Module)**

  * Unicode-compliant Nepali transliteration engine.

  * Modular architecture with TOML-based mapping for easy customization.

  * Autocorrection for common typos via customizable dictionaries.

  * Clean integration with the Fcitx5 input method framework.

  **Key Functions**

  * **Space** → Commits transliterated word. If suggestion commit is enabled, commits the first suggestion. If suggestions are navigated by arrow key, commits the highlighted suggestion.

  * **Enter** → Commits transliterated word if no suggestions. If there are suggestions, commits the highlighted suggestion.

  * **Numbers 1-9** → If there are suggestions, commits the numbered candidate on key press; otherwise, inputs the number. If number transliteration is on, inputs Nepali numbers.

  * **Esc** → Commits the raw typed English text.

  * **Symbols** → If symbols transliteration is on, keys like `*` are transliterated to Nepali symbols. Full stop and question mark always commit the transliterated text, not a word from the suggestion list.

  * **Arrow Up/Down** → Navigates through the suggestion list.

  * **Arrow Left/Right** → Changes cursor position in the input buffer.

* **Dictionary Management (GUI Trainer)**

  * A user-friendly interface to view, add, edit, and delete words in the dictionary.

  * **Learn from Files:** Import text files to automatically learn new Devanagari words and update their frequencies.

  * **Database Tools:** Reset the dictionary or download a pre-trained database to get started quickly.

  * **Live Testing:** Configure and test transliteration rules in real-time without restarting Fcitx5.

## lekhika in action
![img](https://raw.githubusercontent.com/khumnath/fcitx5-lekhika/main/data/Screenshot_fcitx5-lekhika_20250912125709.png)
        
## 📖 Dependencies

To build this project from source, you will need the following development packages.

### ✅ Debian / Ubuntu


`
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build

libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev

qt6-base-dev libsqlite3-dev libicu-dev
`


### ✅ Fedora


`
sudo dnf install rpm-build cmake ninja-build gcc-c++

fcitx5-devel qt6-qtbase-devel sqlite-devel libicu-devel
`


### ✅ Arch Linux / Manjaro


`
sudo pacman -Syu --needed base-devel cmake ninja

fcitx5-qt qt6-base sqlite icu
`

## 🛠️ Build Instructions


`
git clone --recurse-submodules https://github.com/khumnath/fcitx5-lekhika.git
cd fcitx5-lekhika
cmake -B build -G Ninja

-DCMAKE_BUILD_TYPE=Release

-DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
`

## 🚀 Installation

`
sudo cmake --install build
`

To uninstall the project later, you can run `sudo make uninstall` from the `build` directory.

### Prebuilt Packages
> [!NOTE]  
>
> build action file is deleted after try to build packages. for now user should build and install from source. see above

Prebuilt installer packages for Debian/Ubuntu, Fedora, and Arch Linux are available in the [**Releases Section**](https://github.com/khumnath/fcitx5-lekhika/releases).

* For **latest Debian/Ubuntu**, use packages with the `_t64.deb` suffix.

* For **Debian 12 or Ubuntu 22.04**, use the standard `.deb` packages.

## ⚙️ Usage & Activation

### 1. Activating the Input Method

After installation, you need to enable the input method:

1. Run `fcitx5-configtool` (or right-click the Fcitx5 icon in your system tray and choose "Configure").

2. Go to the **Input Method** tab.

3. Click the **+** button in the bottom left.

4. Uncheck "Only Show Current Language".

5. Search for **"Lekhika"** and add it to the list of active input methods.

6. You can now switch to it using your configured hotkey (e.g., `Ctrl+Space`).

### fcitx setting options 
![img](https://raw.githubusercontent.com/khumnath/fcitx5-lekhika/main/data/Screenshot_fcitx5-config_lekhika_20250912130028.png)
    

### 2. Using the Dictionary Trainer

The GUI tool allows you to manage the dictionary that the input method uses.

1. Launch **"Lekhika Trainer"** from your application menu.

2. Use the "Learn Words" tab to import text files and train the dictionary.

3. Use the "Edit Dictionary" tab to manually manage words.

### 📁 Installed File Locations combined ***liblekhika*** module 

| File / Directory | Description |
| ----- | ----- |
| `/usr/bin/lekhika-trainer` | The graphical dictionary manager executable. |
| `/usr/lib/*/fcitx5/` | Input method engine (`fcitx5lekhika.so`). |
| `/usr/lib/*/liblekhika.so.*` | The core transliteration library. |
| `/usr/share/applications/` | App menu entry (`lekhika-trainer.desktop`). |
| `/usr/share/fcitx5/addon/` | Addon configuration (metadata). |
| `/usr/share/fcitx5/inputmethod/` | Input method descriptor. |
| `/usr/share/lekhika-core/` | Runtime data (e.g. `mapping.toml`). |
| `/usr/share/metainfo/` | AppStream metadata for software centers. |
| `/usr/share/icons/hicolor/**/` | Application icons. |
| `~/.local/share/lekhika/` | Your personal dictionary database. |

## 🔧 Configuration

To customize the transliteration rules, you can manually edit the TOML files located in:


`/usr/share/lekhika-core/`


Your changes will take effect after restarting Fcitx5. For dictionary changes testing, it is recommended to use the **Lekhika Trainer** GUI.

## 🤝 Contributing

Pull requests are welcome. If you're working on Nepali input:

* Unicode enhancements

* Packaging for distributions

* Dictionary building

* Transliteration accuracy

* Create an IBus input method using the Lekhika core library.

I’d love your help!

## 📜 License

Licensed under the GNU General Public License v3. See [LICENSE](https://www.gnu.org/licenses/gpl-3.0.html) for details.

## 🔗 Links

* [Fcitx5 Project](https://github.com/fcitx/fcitx5)

* [Fcitx Wiki](https://fcitx-im.org/wiki/)
