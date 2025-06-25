#!/bin/bash
set -e

# Get version from version.txt
VERSION=$(cat version.txt)
ARCHIVE_NAME="fcitx5-lekhika-${VERSION}.tar.gz"

# Define build/output paths
BUILD_DIR=build
DIST_DIR=release-dist
SO_FILE="${BUILD_DIR}/fcitx5lekhika.so"

# Clean and prepare release directory
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"/{config,data,icons/48x48/apps}

# Copy files
cp "$SO_FILE" "$DIST_DIR/"
cp config/fcitx5lekhika.addon.conf "$DIST_DIR/config/"
cp config/fcitx5lekhika.inputmethod.intry.desc "$DIST_DIR/config/"
cp data/*.toml "$DIST_DIR/data/"
cp icons/48x48/apps/*.png "$DIST_DIR/icons/48x48/apps/"

# Create archive
tar -czf "$ARCHIVE_NAME" -C "$DIST_DIR" .

echo "✅ Created release archive: $ARCHIVE_NAME"
