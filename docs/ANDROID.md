# Android notes

The engine has a GLES 3.0 path (`GTA_GLES=1`) but no Android Studio project is
checked in yet. These are the steps to build it with the NDK.

## Prerequisites

* Android NDK r25+ and CMake 3.22+
* SDL2 sources (`SDL2-2.x.y`) or a prebuilt SDL2 Android package
* An Android device or emulator with GLES 3.0

## Add SDL2

Place the SDL2 source tree next to the project and include it before the
executable:

```cmake
add_subdirectory(third_party/SDL2 EXCLUDE_FROM_ALL)
```

SDL2 provides the `SDL2::SDL2` target on Android as well.

## Toolchain configure

```bash
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_shared \
  -DGTA_DATA_DIR=/data/user/0/org.example.gta/files/GTADATA
```

The `ANDROID` branch in `CMakeLists.txt`:

* defines `GTA_GLES=1`;
* links `GLESv3`, `EGL`, `log` and `android`;
* selects the ES 3.0 context and shader translation in `graphics/`.

## Data files

The original `GTADATA` folder must be copied to the device (for example via
`adb push GTADATA /sdcard/Android/data/<package>/files/GTADATA`). At runtime use
either:

```bash
adb shell am start ... --es gta_data /sdcard/Android/data/<package>/files/GTADATA
```

or the `GTA_DATA_DIR` environment variable, or extend the SDL activity to pass
`--data` from the intent extras. `AssetManager::findDataDir()` already checks
executable-relative paths and the current working directory.

## Touch controls

`InputManager` implements a minimal touch layout:

* left half of the screen — virtual steering stick (the touch-down position is
  the origin, drag left/right);
* right half — hold the lower half to accelerate, the upper half to brake;
* lift the finger to release that axis.

No on-screen graphics are drawn for the controls yet; a future HUD layer can
render the stick/pedals using `Font`/`drawRect`-style quads.

## Current limitations

* Test touch layout on a real device; multi-touch tuning may be needed for
  different screen sizes.
* No Android lifecycle handling beyond SDL's default (the activity may be
  recreated; the config and data path are re-read on startup).
* Desktop GLSL is translated to ES 3.0 at compile time; validate shaders on a
  real device if you modify them.
* The audio callback uses the same SDL device code on all platforms.
