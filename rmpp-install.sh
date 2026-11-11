#!/bin/sh
# Install rmpp-hwr on the tablet from the files next to this script (the rmpp installer downloads the release
# assets on the computer, copies them into one folder on the tablet and runs this script there as root).
# Needs rmpp-preload (Vellum package) to load the UI add-on. The release's Korean model (the latest one trained on
# the rmpp-hwr-db data) replaces the one on the tablet; older Korean models are removed (no need to keep them).
set -e
cd "$(dirname "$0")"
D=/home/root/.local/share/rmpp-hwr
mkdir -p "$D"
for f in librmpp_hwr.so libonnxruntime.so; do
	cp "$f" "$D/$f.new" && mv -f "$D/$f.new" "$D/$f"
done
cp glyphs.json "$D/glyphs.json.new" && mv -f "$D/glyphs.json.new" "$D/glyphs.json"   # strokes for Convert to ink
rm -f "$D/librmpp_hwr_ink.so"               # left over from the Convert to ink prototype
if [ -f texts.tar ]; then                   # practice texts per language: $D/texts/<lang>/<topic>.txt
	rm -rf "$D/texts.new" && mkdir -p "$D/texts.new" && tar -xf texts.tar -C "$D/texts.new"
	rm -rf "$D/texts" && mv "$D/texts.new" "$D/texts"
fi
mkdir -p "$D/practice"                      # practice notebook sidecars (the registry), <doc_id>.json
# Default model (Korean): rec.onnx dict.txt model.txt [pack.json] at the top of $D and a copy in models/<id>/, the
# id being the first word of model.txt (e.g. rmpp-hwr-korean-v0.3). Other models of the same language are removed.
if [ -f rec.onnx ] && [ -f model.txt ]; then
	for f in rec.onnx dict.txt model.txt; do
		cp "$f" "$D/$f.new" && mv -f "$D/$f.new" "$D/$f"
	done
	lang=ko
	if [ -f pack.json ]; then lang=$(sed -n 's/.*"language" *: *"\([^"]*\)".*/\1/p' pack.json); fi
	printf '{"language": "%s"}\n' "$lang" > "$D/pack.json"
	id=$(sed -n '1s/ .*//p' model.txt)
	mkdir -p "$D/models"
	for m in "$D"/models/*/; do
		[ -f "$m/pack.json" ] || continue
		if grep -q "\"$lang\"" "$m/pack.json" && [ "$(basename "$m")" != "$id" ]; then rm -rf "$m"; fi
	done
	rm -rf "$D/models/$id" && mkdir -p "$D/models/$id"
	cp "$D/rec.onnx" "$D/dict.txt" "$D/model.txt" "$D/pack.json" "$D/models/$id/"
	rm -f "$D/rec.onnx.prev" "$D/model.txt.prev"
fi
# Language packs (base models of other languages): pack-<id>.tar holds the folder <id>/ with rec.onnx, dict.txt,
# model.txt and pack.json; a pack with the same id is replaced.
for t in pack-*.tar; do
	[ -f "$t" ] || continue
	id=${t#pack-}; id=${id%.tar}
	mkdir -p "$D/models"
	rm -rf "$D/models/$id.new" && mkdir -p "$D/models/$id.new" && tar -xf "$t" -C "$D/models/$id.new"
	rm -rf "$D/models/$id" && mv "$D/models/$id.new/$id" "$D/models/$id" && rm -rf "$D/models/$id.new"
done
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
