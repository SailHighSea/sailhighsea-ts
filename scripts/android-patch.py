#!/usr/bin/env python3
"""Patches the Android project that `tauri android init` generates (src-tauri/gen/android), so CI can build it
without the generated folder being committed:

  1. AndroidManifest.xml: a <queries> entry for magnet: links (Android 11+ package visibility), so the app can find
     torrent apps, plus the INTERNET permission if the template lacks it.
  2. app/build.gradle.kts: a release signing config read from the environment
       ANDROID_KEYSTORE_PATH, ANDROID_KEYSTORE_PASSWORD, ANDROID_KEY_ALIAS, ANDROID_KEY_PASSWORD
     (when ANDROID_KEYSTORE_PATH is not set the release build stays unsigned).

Fails loudly when the template does not look as expected, rather than building a half-patched app.
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "src-tauri/gen/android")

# ---- manifest ----
manifest = root / "app/src/main/AndroidManifest.xml"
text = manifest.read_text(encoding="utf-8")
if 'android:scheme="magnet"' not in text:
    queries = (
        "    <queries>\n"
        '        <intent>\n'
        '            <action android:name="android.intent.action.VIEW" />\n'
        '            <category android:name="android.intent.category.BROWSABLE" />\n'
        '            <data android:scheme="magnet" />\n'
        "        </intent>\n"
        "    </queries>\n\n"
    )
    if "<application" not in text:
        sys.exit("android-patch: no <application> in AndroidManifest.xml")
    text = text.replace("    <application", queries + "    <application", 1) if "    <application" in text else text.replace("<application", queries + "<application", 1)
if "android.permission.INTERNET" not in text:
    text = re.sub(r"(<manifest[^>]*>)", r'\1\n    <uses-permission android:name="android.permission.INTERNET" />', text, count=1)
manifest.write_text(text, encoding="utf-8")

# ---- gradle signing ----
gradle = root / "app/build.gradle.kts"
g = gradle.read_text(encoding="utf-8")
if "signingConfigs" not in g:
    signing = '''    signingConfigs {
        create("release") {
            val ksPath = System.getenv("ANDROID_KEYSTORE_PATH")
            if (ksPath != null) {
                storeFile = file(ksPath)
                storePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("ANDROID_KEY_ALIAS")
                keyPassword = System.getenv("ANDROID_KEY_PASSWORD")
            }
        }
    }
'''
    if not re.search(r"^\s*buildTypes\s*\{", g, re.M):
        sys.exit("android-patch: no buildTypes block in build.gradle.kts")
    g = re.sub(r"^(\s*)buildTypes\s*\{", lambda m: signing + m.group(0), g, count=1, flags=re.M)
    if not re.search(r'getByName\("release"\)\s*\{', g):
        sys.exit('android-patch: no getByName("release") block in build.gradle.kts')
    g = re.sub(
        r'(getByName\("release"\)\s*\{)',
        r'\1\n            if (System.getenv("ANDROID_KEYSTORE_PATH") != null) { signingConfig = signingConfigs.getByName("release") }',
        g,
        count=1,
    )
gradle.write_text(g, encoding="utf-8")
print("android-patch: done")
