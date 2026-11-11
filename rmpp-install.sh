#!/bin/sh
# Install rmpp-hwr on the tablet from the files next to this script (the rmpp installer downloads the release
# assets on the computer, copies them into one folder on the tablet and runs this script there as root).
# Needs rmpp-preload (Vellum package) to load the UI add-on. A model already on the tablet is kept.
set -e
cd "$(dirname "$0")"
D=/home/root/.local/share/rmpp-hwr
mkdir -p "$D"
for f in librmpp_hwr.so libonnxruntime.so; do
	cp "$f" "$D/$f.new" && mv -f "$D/$f.new" "$D/$f"
done
cp glyphs.json "$D/glyphs.json.new" && mv -f "$D/glyphs.json.new" "$D/glyphs.json"   # strokes for Convert to ink
rm -f "$D/librmpp_hwr_ink.so"               # left over from the Convert to ink prototype
if [ ! -f "$D/rec.onnx" ]; then            # keep a personal model that was deployed with `hwr deploy`
	for f in rec.onnx dict.txt model.txt; do
		if [ -f "$f" ]; then cp "$f" "$D/$f.new" && mv -f "$D/$f.new" "$D/$f"; fi
	done
fi
# never `systemctl stop xochitl` (it powers the tablet off); reset-failed avoids the restart limit
mount -o remount,rw /
mkdir -p /usr/lib/rmpp-hwr /usr/lib/rmpp-preload.d
cp librmpp-hwr-ui.so /usr/lib/rmpp-hwr/librmpp-hwr-ui.so.new
mv -f /usr/lib/rmpp-hwr/librmpp-hwr-ui.so.new /usr/lib/rmpp-hwr/librmpp-hwr-ui.so
ln -sf /usr/lib/rmpp-hwr/librmpp-hwr-ui.so /usr/lib/rmpp-preload.d/60-rmpp-hwr.so
sync
systemctl reset-failed xochitl 2>/dev/null || true
systemctl restart xochitl
sleep 3
mount -o remount,ro / || echo "warning: / is still read-write"
echo "rmpp-hwr installed"
