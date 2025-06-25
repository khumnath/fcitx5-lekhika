#!/bin/bash
# bump_version.sh [major|minor|patch]

VERSION_FILE="version.txt"
VERSION=$(cat "$VERSION_FILE")
IFS='.' read -r MAJOR MINOR PATCH <<< "$VERSION"

case "$1" in
  major) ((MAJOR++)); MINOR=0; PATCH=0 ;;
  minor) ((MINOR++)); PATCH=0 ;;
  patch|*) ((PATCH++)) ;;
esac

NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
echo "$NEW_VERSION" > "$VERSION_FILE"
echo "🔼 Version updated to $NEW_VERSION"
