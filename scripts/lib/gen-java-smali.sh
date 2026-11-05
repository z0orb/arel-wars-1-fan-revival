#!/usr/bin/env bash
# Compiles patches/src/*.java to smali for the new-class patch. Usage: gen-java-smali.sh <out-smali-dir>
# Run by hand after changing patches/src. scripts/build only applies the resulting patch.
source "$(dirname "$0")/../env.sh"
out=${1:?usage: gen-java-smali.sh <out-smali-dir>}
# Under the project, because Windows tools can't read Git Bash's /tmp.
tmp=work/gen-java
rm -rf "$tmp"
jar=$(cygpath -m "$LOCALAPPDATA/Android/Sdk/platforms/android-37.0/android.jar")
# A compile-time stand-in for the game's class. It is not packaged.
mkdir -p "$tmp/stub/com/gamevil/nexus2" "$tmp/cls" "$tmp/dex"
cat >"$tmp/stub/com/gamevil/nexus2/NexusGLActivity.java" <<'STUB'
package com.gamevil.nexus2;
public class NexusGLActivity extends android.app.Activity { public static NexusGLActivity myActivity; }
STUB
javac --release 8 -nowarn -cp "$jar" -d "$tmp/cls" "$tmp/stub/com/gamevil/nexus2/NexusGLActivity.java" patches/src/com/gamevil/nexus2/*.java 2>&1 | grep -v -e '^Note' -e 'warning' || true
rm -f "$tmp/cls/com/gamevil/nexus2/NexusGLActivity.class"
d8.bat --lib "$jar" --min-api 21 --output "$tmp/dex" "$tmp"/cls/com/gamevil/nexus2/*.class
(cd "$tmp/dex" && jar cf ../wrap.apk classes.dex)
java -jar "$APKTOOL" d -f --no-res --no-assets -o "$tmp/dec" "$tmp/wrap.apk" >"$tmp/dec.log" 2>&1
find "$tmp/dec/smali" -name '*.smali' -exec sed -i 's/\r$//' {} +
rm -rf "$out"
mkdir -p "$out"
cp -r "$tmp/dec/smali/." "$out/"
find "$out" -type f
