#!/bin/bash
set -e

# This line will be replaced by the release script with the correct version
VERSION=1.0.0

REPO="khumnath/fcitx5-lekhika"
ARCHIVE="fcitx5-lekhika-${VERSION}.tar.gz"
URL="https://github.com/${REPO}/releases/download/${VERSION}/${ARCHIVE}"

TMPDIR=$(mktemp -d)
INSTALL_DIR="/usr/lib/fcitx5"
DATA_DIR="/usr/share/fcitx5/fcitx5-lekhika"
ICON_DIR="/usr/share/icons/hicolor/48x48/apps"

echo "📥 Downloading prebuilt engine version $VERSION..."
cd "$TMPDIR"
curl -sL "$URL" -o "$ARCHIVE"

echo "📦 Extracting archive..."
tar -xzf "$ARCHIVE"

echo "🧩 Installing shared object and configuration files..."
sudo install -Dm755 fcitx5lekhika.so "$INSTALL_DIR/fcitx5lekhika.so"
sudo install -Dm644 config/fcitx5lekhika.addon.conf /usr/share/fcitx5/addon/fcitx5lekhika.conf
sudo install -Dm644 config/fcitx5lekhika.inputmethod.intry.desc /usr/share/fcitx5/inputmethod/fcitx5lekhika.conf

echo "🗂️ Installing transliteration data..."
sudo install -d "$DATA_DIR"
sudo install -m644 data/*.toml "$DATA_DIR"

echo "🎨 Installing icon..."
sudo install -Dm644 icons/48x48/apps/lekhika.png "$ICON_DIR/lekhika.png"

echo "✅ Installation complete! Launch 'fcitx5-configtool' to enable fcitx5-lekhika."

rm -rf "$TMPDIR"
