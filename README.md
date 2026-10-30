# Android 10 Music compatibility project

This is a Gradle wrapper project around the supplied AOSP `packages/apps/Music` snapshot.
The app's Android compatibility values are `minSdk 29`, `targetSdk 29`, `compileSdk 35`.

## Build locally

Install JDK 17, Android SDK Platform 35 and Build Tools 35.0.0, then run:

```bash
gradle assembleDebug
```

APK output: `app/build/outputs/apk/debug/app-debug.apk`.

## Important upstream limitation

The supplied Android 16 AOSP repository snapshot is not a complete standalone Music Player.
Its `MusicBrowserActivity` and `MusicPicker` are placeholders; `MediaPlaybackService` has
empty MediaSession callbacks. It contains the old `AudioPreview` dialog and its resources,
but not full artist/album/playlist browsing, a complete player screen, or persistent
background playback. A minimal song-list launcher has been added to make this port launchable.
See `UPSTREAM_SOURCE.txt` for details. Do not treat this as full feature parity with a
full-featured Music app.
