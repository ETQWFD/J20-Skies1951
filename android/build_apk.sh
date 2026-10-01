#!/usr/bin/env bash
# Build 《长空·1951》 Android FAT APK (arm64-v8a + armeabi-v7a) without gradle:
# NDK clang -> per-ABI libmain.so ; aapt + d8 + zipalign + apksigner -> signed APK.
# minSdk 24 (Android 7.0) so 32-bit / older phones can install it too.
set -euo pipefail

PROJ=/home/user/Doubao/chats/38443589649431042/J20_Skies1951
AND=/home/user/android
NDK=$AND/android-ndk-r25c
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
SDK=$AND/sdk
BT=$SDK/build-tools/34.0.0
ANDROID_JAR=$SDK/platforms/android-33/android.jar
API=24          # native + minSdk (Android 7.0)
TARGETSDK=33

PKG=com.changkong1951.skies
PKGPATH=com/changkong1951/skies
LABEL="长空·1951"
KEYPASS=skies1951

STAGE=$PROJ/android/apk
OUT=$PROJ/android/out
RLSRC=$PROJ/third_party/raylib/src
GLUE=$NDK/sources/android/native_app_glue
FONTDIR=$PROJ/third_party/font

# ABI "name:ccPrefix:fontObj"
ABIS=(
  "arm64-v8a|aarch64-linux-android|fontdata_android.o|arm64-v8a"
  "armeabi-v7a|armv7a-linux-androideabi|fontdata_android_arm.o|armeabi-v7a"
)

rm -rf "$STAGE" "$OUT"
mkdir -p "$STAGE"/{gen,bin,assets,res/values,obj-cls} "$OUT"

CFLAGS="-Os -fPIC -std=c11 -DANDROID -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 \
 -D__ANDROID_API__=$API -DFONT_EMBEDDED -ffunction-sections -fdata-sections \
 -Wall -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable \
 -Wno-unused-result -Wno-misleading-indentation -Wno-unknown-attributes \
 -Wno-deprecated-declarations \
 -I$PROJ/src -I$RLSRC -I$GLUE"

LDFLAGS="-shared -Wl,-soname,libmain.so -Wl,--build-id -Wl,-z,noexecstack \
 -u ANativeActivity_onCreate -Wl,--gc-sections"
LIBS="-lm -llog -landroid -lEGL -lGLESv2 -lOpenSLES -ldl"

# ---- 1. native compile per ABI ----
for entry in "${ABIS[@]}"; do
  IFS='|' read -r ABI CCprefix FONTOBJ RLDIR <<< "$entry"
  CC=$TC/bin/${CCprefix}${API}-clang
  RLIB=$PROJ/third_party/android/$RLDIR/libraylib.a
  mkdir -p "$STAGE/lib/$ABI" "$STAGE/obj/$ABI"
  echo "== [$ABI] compiler: $(basename $CC) =="
  OBJS=""
  for src in $PROJ/src/*.c; do
    o=$STAGE/obj/$ABI/$(basename "$src" .c).o
    echo "CC $(basename "$src")"
    $CC $CFLAGS -c "$src" -o "$o"
    OBJS="$OBJS $o"
  done
  echo "LINK libmain.so ($ABI)"
  $CC $LDFLAGS -o "$STAGE/lib/$ABI/libmain.so" $OBJS "$FONTDIR/$FONTOBJ" "$RLIB" $LIBS
  ls -lh "$STAGE/lib/$ABI/libmain.so"
done

# ---- 2. manifest / resources / java loader ----
cat > "$STAGE/AndroidManifest.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="$PKG" android:versionCode="14" android:versionName="2.1.0">
    <uses-sdk android:minSdkVersion="$API" android:targetSdkVersion="$TARGETSDK" />
    <uses-feature android:glEsVersion="0x00020000" android:required="true" />
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />
    <application android:allowBackup="false" android:label="@string/app_name"
        android:icon="@drawable/icon" android:hasCode="true"
        android:extractNativeLibs="true">
        <activity android:name="$PKG.NativeLoader"
            android:theme="@android:style/Theme.NoTitleBar.Fullscreen"
            android:configChanges="orientation|keyboardHidden|screenSize|keyboard"
            android:screenOrientation="landscape" android:launchMode="singleTask"
            android:exported="true">
            <meta-data android:name="android.app.lib_name" android:value="main" />
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
EOF

cat > "$STAGE/res/values/strings.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<resources><string name="app_name">$LABEL</string></resources>
EOF

for d in ldpi mdpi hdpi xhdpi xxhdpi; do
  src="$PROJ/android/icons/drawable-$d/icon.png"
  mkdir -p "$STAGE/res/drawable-$d"
  cp -f "$src" "$STAGE/res/drawable-$d/icon.png"
done

mkdir -p "$STAGE/src/$PKGPATH"
cat > "$STAGE/src/$PKGPATH/NativeLoader.java" <<EOF
package $PKG;
public class NativeLoader extends android.app.NativeActivity {
    static { System.loadLibrary("main"); }
}
EOF

# ---- 3. R.java, compile java, dex ----
"$BT/aapt" package -f -m -J "$STAGE/gen" -S "$STAGE/res" \
 -M "$STAGE/AndroidManifest.xml" -I "$ANDROID_JAR"
javac -source 8 -target 8 -nowarn -classpath "$ANDROID_JAR" -d "$STAGE/obj-cls" \
 $(find "$STAGE/gen" "$STAGE/src" -name "*.java")
"$BT/d8" --min-api $API --lib "$ANDROID_JAR" --output "$STAGE/bin" \
 $(find "$STAGE/obj-cls" -name "*.class")

# ---- 4. assemble unsigned apk, add both native libs ----
cd "$STAGE"
"$BT/aapt" package -f -M AndroidManifest.xml -S res -A assets -I "$ANDROID_JAR" \
 -F bin/app.unsigned.apk bin
for entry in "${ABIS[@]}"; do
  IFS='|' read -r ABI _ <<< "$entry"
  "$BT/aapt" add bin/app.unsigned.apk lib/$ABI/libmain.so
done

# ---- 5. align + sign ----
"$BT/zipalign" -f -p 4 bin/app.unsigned.apk bin/app.aligned.apk
[ -f "$OUT/skies.keystore" ] || keytool -genkeypair -validity 10000 \
 -dname "CN=ChangKong1951,O=Skies,C=CN" -keystore "$OUT/skies.keystore" \
 -storepass "$KEYPASS" -keypass "$KEYPASS" -alias skies -keyalg RSA -keysize 2048
"$BT/apksigner" sign --ks "$OUT/skies.keystore" --ks-pass "pass:$KEYPASS" \
 --key-pass "pass:$KEYPASS" --out "$OUT/J20_Skies1951.apk" bin/app.aligned.apk
echo "=== verify ==="
"$BT/apksigner" verify --verbose "$OUT/J20_Skies1951.apk" | head -6
echo "=== APK ==="; ls -lh "$OUT/J20_Skies1951.apk"
"$BT/aapt" list "$OUT/J20_Skies1951.apk" | grep -E "lib/.*/libmain|classes.dex|AndroidManifest"
