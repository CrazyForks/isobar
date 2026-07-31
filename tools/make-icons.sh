#!/bin/sh
# Regenerate the app icon set from the SVG source.
# Run on macOS only (uses sips + iconutil). Pillow (pip install Pillow)
# is needed for the Windows .ico and the Linux PNG.
#
#   sh tools/make-icons.sh [path/to/icon.svg]
#
# Output goes to assets/. Commit the results — CI does not regenerate them
# (iconutil is macOS-only; keeping a SVG renderer out of CI is simpler).

set -e
SRC="${1:-jmh-portrait.svg}"
OUT=assets
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$OUT"

# --- macOS .icns (sips renders SVG; iconutil packs the .iconset) ---
ICONSET="$TMP/isobar.iconset"
mkdir -p "$ICONSET"
sips -s format png -z 1024 1024 "$SRC" --out "$ICONSET/icon_512x512@2x.png" >/dev/null
for sz in 16 32 128 256 512; do
    double=$((sz * 2))
    sips -z "$sz" "$sz"      "$ICONSET/icon_512x512@2x.png" --out "$ICONSET/icon_${sz}x${sz}.png"   >/dev/null
    sips -z "$double" "$double" "$ICONSET/icon_512x512@2x.png" --out "$ICONSET/icon_${sz}x${sz}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$OUT/isobar.icns"
echo "wrote $OUT/isobar.icns"

# --- Windows .ico + Linux PNG (Pillow) ---
MASTER="$TMP/master.png"
sips -s format png -z 1024 1024 "$SRC" --out "$MASTER" >/dev/null
python3 - "$MASTER" "$OUT" <<'PY'
import sys
from PIL import Image
master, out = sys.argv[1], sys.argv[2]
img = Image.open(master).convert("RGBA")
img.save(f"{out}/isobar.ico", format="ICO",
         sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])
img.resize((512,512), Image.LANCZOS).save(f"{out}/isobar-512.png")
img.resize((128,128), Image.LANCZOS).save(f"{out}/isobar-128.png")
print(f"wrote {out}/isobar.ico + {out}/isobar-512.png + {out}/isobar-128.png")
PY

echo "done. (commit assets/ — CI copies these into the bundle as-is.)"