#!/usr/bin/env bash
# Embody packaging + deploy.
#   ./package.sh            dev build  (logs to C:\Embody.log), zip + deploy to MO2
#   ./package.sh --release  silent build (-DEMBODY_LOG=0), zip only (no deploy)
#   ./package.sh --no-deploy build + zip, skip the MO2 copy
#
# Produces the mod in MO2 / Nexus layout — the archive root IS the game Data folder:
#   Embody.esp
#   SKSE/Plugins/Embody.dll
#   MCM/Config/Embody/{config.json,settings.ini}
# If it deploys cleanly through MO2's VFS like this, it installs the same on Vortex or a manual (VFS-less) setup.
set -euo pipefail

VERSION="0.54"    # pre-1.0: core (Capability A) stable & public; 1.0 = full Enhanced Camera parity (Capability B)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/Embody.cpp"
ESP="$HERE/mcm/Data/Embody.esp"
MCMDIR="$HERE/mcm/Data/MCM/Config/Embody"
DIST="$HERE/dist"
STAGE="$DIST/stage"                                 # archive-root staging (no MO2 meta.ini)
MODS="/home/greasenipple/filesLibrary/SteamLibrary/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/AppData/Local/ModOrganizer/Skyrim Special Edition/mods"

RELEASE=0; DEPLOY=1
for a in "$@"; do
  case "$a" in
    --release)   RELEASE=1; DEPLOY=0 ;;            # release archives shouldn't be auto-deployed to the dev instance
    --no-deploy) DEPLOY=0 ;;
    *) echo "unknown arg: $a" >&2; exit 2 ;;
  esac
done

# 1. Build the DLL fresh so the packaged one is always current.
CXX=x86_64-w64-mingw32-g++
LOGFLAG=""; BUILDKIND="dev (logging ON)"
if [ "$RELEASE" = 1 ]; then LOGFLAG="-DEMBODY_LOG=0"; BUILDKIND="RELEASE (silent)"; fi
echo ">> building $BUILDKIND"
"$CXX" -shared -O2 -nostdlib -Wl,--entry,DllMain $LOGFLAG -o "$HERE/Embody.dll" "$SRC" -lkernel32 -luser32
echo "   Embody.dll $(stat -c%s "$HERE/Embody.dll") bytes"

# 2. Assemble the archive-root staging tree.
rm -rf "$STAGE"
mkdir -p "$STAGE/SKSE/Plugins" "$STAGE/MCM/Config/Embody"
cp "$HERE/Embody.dll"      "$STAGE/SKSE/Plugins/Embody.dll"
cp "$ESP"                  "$STAGE/Embody.esp"
cp "$MCMDIR/config.json"   "$STAGE/MCM/Config/Embody/config.json"
cp "$MCMDIR/settings.ini"  "$STAGE/MCM/Config/Embody/settings.ini"

# 3. Zip it (Nexus / Vortex / manual installable archive).
ZIP="$DIST/Embody-$VERSION.zip"
rm -f "$ZIP"
python3 - "$STAGE" "$ZIP" <<'PY'
import sys, os, zipfile
stage, zippath = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zippath, 'w', zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk(stage):
        for f in sorted(files):
            full = os.path.join(root, f)
            z.write(full, os.path.relpath(full, stage))
PY
echo ">> archive: $ZIP"
( cd "$STAGE" && find . -type f | sed 's|^\./|   |' )

# 4. Deploy into the MO2 mods folder (mirror staging, plus a meta.ini so MO2 shows it as a managed mod).
if [ "$DEPLOY" = 1 ]; then
  if [ ! -d "$MODS" ]; then echo "!! MO2 mods dir not found: $MODS" >&2; exit 1; fi
  DEST="$MODS/Embody"
  mkdir -p "$DEST"
  # replace only our payload; leave any MO2 bookkeeping intact
  rm -rf "$DEST/SKSE" "$DEST/MCM" "$DEST/Embody.esp"
  cp -r "$STAGE/." "$DEST/"
  if [ ! -f "$DEST/meta.ini" ]; then
    printf '[General]\ngameName=SkyrimSE\nversion=%s\ncategory=0\n' "$VERSION" > "$DEST/meta.ini"
  fi
  echo ">> deployed to MO2: $DEST"
  echo "   (enable 'Embody' in the MO2 left pane, and tick Embody.esp in the right pane)"
fi
echo ">> done ($VERSION)"
