#!/usr/bin/env bash
set -euo pipefail

# Get version (strip leading 'v')
TAG=${GITHUB_REF_NAME:-$(git describe --tags --abbrev=0)}
VERSION=${TAG#v}
DATE=$(date +%Y-%m-%d)

# Files to update
FILES=(
  "data/fcitx5-lekhika.metainfo.xml"
  "data/lekhika-trainer.metainfo.xml"
)

for FILE in "${FILES[@]}"; do
  if [[ ! -f "$FILE" ]]; then
    echo "Skipping missing $FILE"
    continue
  fi

  echo "Updating $FILE with version $VERSION ($DATE)"

  # Insert new release entry right after <releases>
  # Backup first, then overwrite
  cp "$FILE" "$FILE.bak"

  awk -v ver="$VERSION" -v date="$DATE" '
    /<releases>/ && !done {
      print
      print "    <release version=\"" ver "\" date=\"" date "\">"
      print "      <description>"
      print "        <p>Automated release from github CI.</p>"
      print "      </description>"
      print "    </release>"
      done=1
      next
    }
    {print}
  ' "$FILE.bak" > "$FILE"
done
