
# fcitx5-lekhika

**fcitx5-lekhika** is a transliteration-based input method module for [Fcitx5](https://github.com/fcitx/fcitx5), focused on improving Nepali typing workflows using a phonetic or rule-based system. It is designed for performance, accuracy, and extensibility.

---

## ✨ Features

- Unicode-compliant Nepali transliteration engine  
- Modular architecture with TOML-based mapping configuration  
- Autocorrection support via customizable dictionaries  
- Clean integration with the Fcitx5 input method framework

---

## 📦 Dependencies

### ✅ Debian / Ubuntu

Install the required development packages:

```bash
sudo apt-get install build-essential cmake fcitx5 fcitx5-config-qt libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev
```

### ✅ Arch Linux / Manjaro

```bash
sudo pacman -S base-devel cmake fcitx5 fcitx5-configtool fcitx5-base
```

---

## 🛠️ Build Instructions

```bash
git clone https://github.com/khumnath/fcitx5-lekhika.git
cd fcitx5-lekhika
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
```

---

## 🚀 Installation

```bash
sudo make install
```

---

## 🗂️ Installed File Locations

| File / Directory                      | Description                         |
|--------------------------------------|-------------------------------------|
| `/usr/lib/fcitx5/fcitx5lekhika.so`   | Input method engine shared object   |
| `/usr/share/fcitx5/addon/`           | Addon configuration (metadata)      |
| `/usr/share/fcitx5/inputmethod/`     | Input method descriptor             |
| `/usr/share/fcitx5/fcitx5-lekhika/`  | Runtime data (e.g. TOML mappings)   |
| `/usr/share/icons/hicolor/48x48/`    | Application icons (optional)        |

---

## ⚙️ Activation

After installation:
1. Run `fcitx5-configtool` or `fcitx5-config-qt`  
2. Enable **लेखिका** from the *Input Method* tab  
3. Optionally reorder or customize keybindings

---

## 🧩 Configuration

Edit `mapping.toml` and `autocorrect.toml` in:

```bash
/usr/share/fcitx5/fcitx5-lekhika/
```

Your changes will take effect after restarting Fcitx5.

---

## 🤝 Contributing

Pull requests are welcome. If you're working on nepali input :
- Unicode enhancements  
- Transliteration accuracy  
- Fcitx5 UI integrations or metadata  

…I’d love your help!

---

## 📜 License

Licensed under the GNU General Public License v3.  
See [LICENSE](./LICENSE) for details.

---

## 🌐 Links

- [Fcitx5 Project](https://github.com/fcitx/fcitx5)
- [Fcitx Wiki](https://fcitx-im.org/wiki/)
