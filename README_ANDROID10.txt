Android 10 Music source package
===============================

Build:
  gradle assembleDebug

APK output:
  app/build/outputs/apk/debug/app-debug.apk

This is a standalone Gradle conversion of the supplied AOSP Music source snapshot.
The app targets Android 10 (API 29). It keeps the upstream AudioPreview Java source
and resources and adds a minimal launcher screen to list local audio files.

Important limitation:
The supplied Android 16 AOSP snapshot itself has no complete Music browser UI.
Its MusicBrowserActivity and MusicPicker are placeholders, and MediaPlaybackService
has no working playback callbacks. Therefore this port cannot preserve full playlists,
artist/album pages, a full player UI, or persistent background playback. Those features
are absent from the supplied source, not disabled by Android 10. See UPSTREAM_SOURCE.txt.

The Kotlin folder is retained under AOSP_REFERENCE as reference-only source; the app
compiles the Java implementation to avoid duplicate class names.
