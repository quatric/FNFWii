#!/bin/sh
# Runs FunkinWii inside Dolphin with a scripted autotest.txt and saves the
# screenshots it asks for as PNGs.
#
#   scripts/autotest.sh OUTDIR SCRIPT.txt
#
# SCRIPT.txt lines (times in seconds from start):
#   scene title|mainmenu|story|freeplay|options   first scene
#   play SONG [DIFFICULTY] [VARIATION]           start straight in a song
#   botplay                                      let the game hit the notes
#   press T accept|back|pause|left|down|up|right
#   seek T MS                                    jump to MS in the running song
#   shot T NAME                                  save OUTDIR/NAME.png
#   quit T
#
# Needs: `make dist` (dist/apps/FunkinWii with data/), mtools and Dolphin.
# DOLPHIN overrides Dolphin's path, GFX the video backend (Metal by default:
# the game draws with GX, so the Null backend would leave the screen black).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out=$1
script=$2
app="$here/dist/apps/FunkinWii"
dolphin=${DOLPHIN:-/Applications/Dolphin.app/Contents/MacOS/Dolphin}
[ -f "$app/boot.dol" ] && [ -d "$app/data" ] || { echo "run make dist first" >&2; exit 1; }
mkdir -p "$out"
work="$out/.dolphin"
rm -rf "$work"; mkdir -p "$work/user/Config"

# The SD image with the game data is built once and reused while dist/ is unchanged.
base="$here/build/sdbase.img"
stamp="$here/build/sdbase.stamp"
newest=$(find "$app" -newer "$stamp" -print -quit 2>/dev/null || echo x)
if [ ! -f "$base" ] || [ ! -f "$stamp" ] || [ -n "$newest" ]; then
    echo "building SD image..."
    size=$(du -sm "$app" | cut -f1)
    rm -f "$base"
    dd if=/dev/zero of="$base" bs=1m count=0 seek=$((size + 256)) 2>/dev/null
    mformat -i "$base" -F ::
    mmd -i "$base" ::/apps
    mcopy -s -i "$base" "$app" ::/apps/
    touch "$stamp"
fi
sd="$work/sd.img"
cp -c "$base" "$sd" 2>/dev/null || cp "$base" "$sd"
mcopy -i "$sd" "$script" ::/apps/FunkinWii/autotest.txt

cat > "$work/user/Config/Dolphin.ini" <<EOF
[General]
WiiSDCardPath = $sd
[Core]
WiiSDCard = True
WiiSDCardAllowWrites = True
WiiSDCardEnableFolderSync = False
GFXBackend = ${GFX:-Metal}
[Interface]
ConfirmStop = False
UsePanicHandlers = False
[DSP]
Backend = No Audio Output
DumpAudio = ${AUDIO:-False}
DumpAudioSilent = ${AUDIO:-False}
[Analytics]
Enabled = False
PermissionAsked = True
EOF
cat > "$work/user/Config/GFX.ini" <<EOF
[Hacks]
EFBToTextureEnable = False
XFBToTextureEnable = False
ImmediateXFBEnable = False
EOF

"$dolphin" -u "$work/user" -b -e "$app/boot.dol" -v "${GFX:-Metal}" >"$work/dolphin.log" 2>&1 &
pid=$!
i=0
limit=${TIMEOUT:-180}
while [ $i -lt "$limit" ]; do
    sleep 1
    i=$((i + 1))
    kill -0 $pid 2>/dev/null || break
    if mdir -i "$sd" ::/apps/FunkinWii/autotest.done >/dev/null 2>&1; then break; fi
done
sleep 1
kill $pid 2>/dev/null || true          # Dolphin asks for a second signal before it stops
sleep 1; kill $pid 2>/dev/null || true
sleep 2; kill -9 $pid 2>/dev/null || true
wait $pid 2>/dev/null || true

mcopy -o -i "$sd" ::/apps/FunkinWii/log.txt "$out/log.txt" 2>/dev/null || true
mcopy -o -i "$sd" ::/apps/FunkinWii/shots/state.txt "$out/state.txt" 2>/dev/null || true
for f in $(mdir -b -i "$sd" ::/apps/FunkinWii/shots 2>/dev/null | grep '\.yuv$' || true); do
    n=$(basename "$f" .yuv)
    mcopy -o -i "$sd" "$f" "$work/$n.yuv"
    python3 -I "$here/scripts/yuv2png.py" "$work/$n.yuv" "$out/$n.png"
    echo "$out/$n.png"
done
mdir -i "$sd" ::/apps/FunkinWii/autotest.done >/dev/null 2>&1 && echo "finished" || echo "did not finish (timeout or crash)"
exit 0
