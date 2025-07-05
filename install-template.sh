#!/bin/bash
set -eEuo pipefail

# This line will be replaced by the release script with the correct version
VERSION=.*

REPO="khumnath/fcitx5-lekhika"
ARCHIVE="fcitx5-lekhika-${VERSION}.tar.gz"
URL="https://github.com/${REPO}/releases/download/${VERSION}/${ARCHIVE}"

TMPDIR=$(mktemp -d)
INSTALL_DIR="/usr/lib/x86_64-linux-gnu/fcitx5"
DATA_DIR="/usr/share/fcitx5/fcitx5-lekhika"
ICON_DIR="/usr/share/icons/hicolor/48x48/apps"

# Cleanup on error
trap 'echo "⚠️ Installation failed. Cleaning up..."; rm -rf "$TMPDIR"; exit 1' ERR

echo "📥 Downloading prebuilt engine version $VERSION..."
cd "$TMPDIR"
if ! curl -sL "$URL" -o "$ARCHIVE"; then
  echo "❌ Failed to download $ARCHIVE from $URL"
  echo "   Ensure the version number is correct and the release exists."
  exit 1
fi
echo "✅ Archive downloaded: $ARCHIVE"

echo "📦 Extracting archive..."
if ! tar -xzf "$ARCHIVE"; then
  echo "❌ Failed to extract $ARCHIVE"
  echo "   The archive may be corrupt or missing expected files."
  exit 1
fi
echo "✅ Extraction complete"

echo "🧩 Installing engine binary..."
sudo install -Dm755 fcitx5lekhika.so "$INSTALL_DIR/fcitx5lekhika.so" && \
  echo "✅ Installed: $INSTALL_DIR/fcitx5lekhika.so"

echo "🛠️ Installing configuration files..."
sudo install -Dm644 config/fcitx5lekhika.addon.conf /usr/share/fcitx5/addon/fcitx5lekhika.conf && \
  echo "✅ Installed: /usr/share/fcitx5/addon/fcitx5lekhika.conf"

sudo install -Dm644 config/fcitx5lekhika.conf /usr/share/fcitx5/inputmethod/fcitx5lekhika.conf && \
  echo "✅ Installed: /usr/share/fcitx5/inputmethod/fcitx5lekhika.conf"

echo "🗂️ Installing transliteration data..."
sudo install -d "$DATA_DIR"
sudo install -m644 data/*.toml "$DATA_DIR" && \
  echo "✅ Installed TOML files to: $DATA_DIR"

echo "🎨 Installing icon..."
sudo install -Dm644 icons/48x48/apps/lekhika.png "$ICON_DIR/lekhika.png" && \
  echo "✅ Installed icon: $ICON_DIR/lekhika.png"

echo "🎨 Updating font cache..."
sudo fc-cache -fv > /dev/null && echo "✅ Font cache updated"

echo ""
echo "📍 Summary of installed components:"
echo " - Engine: $INSTALL_DIR/fcitx5lekhika.so"
echo " - Configs:"
echo "     • /usr/share/fcitx5/addon/fcitx5lekhika.conf"
echo "     • /usr/share/fcitx5/inputmethod/fcitx5lekhika.conf"
echo " - Transliteration data: $DATA_DIR"
echo " - Icon: $ICON_DIR/lekhika.png"
echo ""

echo "✅ Installation complete! Use 'fcitx5-configtool' to enable fcitx5-lekhika."

rm -rf "$TMPDIR"

echo "✅ Temporary files cleaned"
