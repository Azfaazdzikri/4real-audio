#!/bin/bash
# Builds ONE installer (4real-audio-<version>-macOS.pkg) from the built plugin bundles.
#   usage: installer/macos/build_pkg.sh <version> <artefacts dir> <output dir>
#   <artefacts dir> holds VST3/4real-audio.vst3, AU/4real-audio.component and Standalone/4real-audio.app
# The installer puts them in /Library/Audio/Plug-Ins/VST3, /Library/Audio/Plug-Ins/Components and /Applications, and lets
# the player untick a part ("Customize"). Run on macOS (needs codesign, pkgbuild, productbuild).
set -euo pipefail

VERSION="${1:?version}"
ART="${2:?artefacts dir}"
OUT="${3:?output dir}"
ID="com.fourreal.audio"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

declare -a PARTS=(
  "vst3|VST3/4real-audio.vst3|Library/Audio/Plug-Ins/VST3/4real-audio.vst3"
  "au|AU/4real-audio.component|Library/Audio/Plug-Ins/Components/4real-audio.component"
  "standalone|Standalone/4real-audio.app|Applications/4real-audio.app"
)

for part in "${PARTS[@]}"; do
  IFS='|' read -r name src dest <<<"$part"
  if [ ! -e "$ART/$src" ]; then
    echo "missing $ART/$src"; find "$ART" -maxdepth 3 | head -50; exit 1
  fi
  # Ad-hoc signature: Apple Silicon refuses to load unsigned code. (A paid Apple Developer ID would let the installer be notarized.)
  codesign --force --deep --sign - "$ART/$src"

  root="$WORK/root-$name"
  mkdir -p "$root/$(dirname "$dest")"
  cp -R "$ART/$src" "$root/$dest"

  # "not relocatable" = always install to the folder above, never "update" a copy of the plugin that happens to sit somewhere else
  cat >"$WORK/$name.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><array><dict>
  <key>RootRelativeBundlePath</key><string>$dest</string>
  <key>BundleIsRelocatable</key><false/>
  <key>BundleIsVersionChecked</key><false/>
  <key>BundleHasStrictIdentifier</key><false/>
  <key>BundleOverwriteAction</key><string>upgrade</string>
</dict></array></plist>
PLIST

  pkgbuild --root "$root" --install-location / --component-plist "$WORK/$name.plist" \
           --identifier "$ID.$name" --version "$VERSION" "$WORK/$name.pkg"
done

cat >"$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>4real-audio $VERSION</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_anywhere="false" enable_localSystem="true"/>
  <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
  <choices-outline>
    <line choice="vst3"/>
    <line choice="au"/>
    <line choice="standalone"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plugin" description="For DAWs that use VST3 (Studio One, Reaper, Cubase, Bitwig ...)." start_selected="true">
    <pkg-ref id="$ID.vst3"/>
  </choice>
  <choice id="au" title="Audio Unit plugin" description="For Logic Pro, GarageBand, MainStage and other Audio Unit hosts." start_selected="true">
    <pkg-ref id="$ID.au"/>
  </choice>
  <choice id="standalone" title="Standalone app" description="Plays your guitar through 4real-audio without a DAW (Applications folder)." start_selected="true">
    <pkg-ref id="$ID.standalone"/>
  </choice>
  <pkg-ref id="$ID.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
  <pkg-ref id="$ID.au" version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
  <pkg-ref id="$ID.standalone" version="$VERSION" onConclusion="none">standalone.pkg</pkg-ref>
</installer-gui-script>
XML

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT/4real-audio-$VERSION-macOS.pkg"
echo "Built $OUT/4real-audio-$VERSION-macOS.pkg"
