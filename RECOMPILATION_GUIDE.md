# Руководство по реинжинирингу Grand Theft Auto (DOS) для кроссплатформы

## Обзор проекта

Целью является преобразование классической DOS версии GTA в современное кроссплатформенное приложение с поддержкой:
- **Платформы**: Windows, macOS, Linux, Android
- **Новые возможности**: произвольные разрешения, оконный режим, сглаживание (AA/MSAA), поддержка геймпадов, улучшенная графика

---

## Фаза 1: Подготовка и анализ

### 1.1 Извлечение и анализ исходного кода
1. Распаковать `Grand_Theft_Auto_ISO.rar`
2. Смонтировать ISO образ или распаковать содержимое
3. Локализовать исходный код игры (обычно в формате ASM x86 или скомпилированные бинари)
4. Если исходник не доступен, использовать reverse engineering инструменты:
   - **IDA Pro** или **Ghidra** - для дизассемблирования бинарников
   - **Dosbox-X** с отладчиком - для отслеживания логики игры
   - **Hex editors** - для анализа структур данных

### 1.2 Документирование архитектуры
- Создать схему основных компонентов (рендеринг, физика, AI, звук)
- Идентифицировать точки входа (main, game loop)
- Отслеить взаимодействия с OS (прерывания, системные вызовы)
- Документировать форматы данных (графика, музыка, уровни)

---

## Фаза 2: Выбор архитектуры и инструментов

### 2.1 Рекомендуемый стек технологий

#### Вариант A: C++ + SDL2 (Рекомендуется для кроссплатформы)
```
Язык: C++17 или C++20
Графика: OpenGL 3.3+ / Vulkan
Аудио: OpenAL-soft
Input: SDL2 (клавиатура, мышь, геймпад)
Build: CMake
Platform: Windows, macOS, Linux, Android (NDK)
```

**Преимущества**: максимальная совместимость, хорошая производительность

#### Вариант B: Rust + Winit + WGPU
```
Язык: Rust
Графика: WGPU (GPU abstraction)
Платформа: Desktop + Web (WASM)
```

**Преимущества**: безопасность памяти, современный экосистем

#### Вариант C: C# + MonoGame / FNA
```
Язык: C#
Framework: MonoGame или FNA (воссоздание XNA API)
Платформа: Windows, macOS, Linux, Android, iOS
```

**Преимущества**: быстрая разработка, готовые решения

### 2.2 Выбранный подход: **C++ + SDL2 + OpenGL**

Причины:
- Прямой контроль над памятью (важно для оптимизации)
- Родство с исходным кодом (также x86 код)
- Лучшая совместимость с legacy кодом
- Отличная поддержка всех требуемых платформ

---

## Фаза 3: Структура проекта

```
dos_gta_recomp/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                 # Точка входа
│   ├── core/
│   │   ├── game.h/cpp           # Главный game loop
│   │   ├── engine.h/cpp         # Ядро движка
│   │   └── config.h/cpp         # Конфигурация (разрешение, FPS и т.д.)
│   ├── graphics/
│   │   ├── renderer.h/cpp       # OpenGL рендерер
│   │   ├── shader.h/cpp         # Шейдеры (GLSL)
│   │   ├── texture.h/cpp        # Управление текстурами
│   │   └── camera.h/cpp         # Камера
│   ├── input/
│   │   ├── input_manager.h/cpp  # Ввод (клавиатура, мышь)
│   │   └── gamepad.h/cpp        # Поддержка геймпадов (SDL2)
│   ├── audio/
│   │   └── audio_manager.h/cpp  # OpenAL-soft
│   ├── physics/
│   │   └── physics.h/cpp        # Физика/коллизии
│   ├── game/
│   │   ├── world.h/cpp          # Мир игры
│   │   ├── vehicle.h/cpp        # Машины
│   │   ├── player.h/cpp         # Игрок/персонажи
│   │   └── ai.h/cpp             # ИИ
│   ├── data/
│   │   ├── loader.h/cpp         # Загрузка ассетов
│   │   └── formats.h             # Структуры данных GTA
│   └── platform/
│       ├── platform.h            # Абстракция платформы
│       ├── windows.h/cpp
│       ├── macos.h/cpp
│       ├── linux.h/cpp
│       └── android.h/cpp
├── assets/
│   ├── shaders/
│   │   ├── vertex.glsl
│   │   └── fragment.glsl
│   └── data/                    # GTA data файлы
├── tests/
├── docs/
└── build/
```

---

## Фаза 4: Реинжиниринг компонентов

### 4.1 Подсистема рендеринга

#### Шаг 1: Базовое окно и контекст OpenGL
```cpp
// src/graphics/renderer.h
class Renderer {
public:
    Renderer(int width, int height, bool windowed);
    void initialize();
    void beginFrame();
    void endFrame();
    void resize(int width, int height);
    void setAntiAliasing(int samples); // 0, 2, 4, 8, 16
    
private:
    SDL_Window* window;
    SDL_GLContext glContext;
    GLuint framebuffer; // для AA
};
```

#### Шаг 2: Шейдеры для современной графики
- **Вершинный шейдер**: трансформация координат, поддержка камеры
- **Фрагментный шейдер**: текстурирование, освещение, AA фильтры
- **Post-processing**: сглаживание FXAA/SMAA, гамма коррекция

#### Шаг 3: Система текстур
- Преобразовать исходные sprite'ы в современные форматы (PNG, DDS)
- Реализовать кэширование и управление VRAM
- Поддержка разных разрешений (масштабирование, суперсемплинг)

### 4.2 Система ввода

```cpp
// src/input/input_manager.h
class InputManager {
public:
    void update();
    bool isKeyPressed(SDL_Scancode key);
    bool isGamepadConnected(int id);
    GamepadState getGamepadState(int id);
    MouseState getMouseState();
    
    // Переназначение клавиш
    void remapKey(GameAction action, SDL_Scancode key);
    void remapGamepadButton(GameAction action, SDL_GameControllerButton btn);
};

enum class GameAction {
    MoveForward, MoveBackward, MoveLeft, MoveRight,
    Accelerate, Brake, Handbrake,
    Attack, Jump, Enter,
    Menu, Pause
};
```

**Поддерживаемые геймпады**:
- Xbox 360/One/Series
- PlayStation 4/5
- Nintendo Switch Pro
- Generic gamepads

### 4.3 Подсистема аудио

```cpp
// src/audio/audio_manager.h
class AudioManager {
public:
    void initialize();
    void playSound(const std::string& name, float volume = 1.0f);
    void playMusic(const std::string& track, bool loop = true);
    void setMasterVolume(float vol);
    void setPaused(bool paused);
};
```

Использовать **OpenAL-soft** для кроссплатформенности.

### 4.4 Физика и коллизии

Опции:
- **Вариант 1**: Портировать исходный код физики из DOS версии
- **Вариант 2**: Использовать готовую библиотеку (Bullet, ODE, Rapier)

**Рекомендация**: Вариант 1 с обертками для оптимизации

---

## Фаза 5: Загрузка ассетов

### 5.1 Форматы данных GTA (DOS)
- **.spr** - спрайты
- **.map** - уровни/карты
- **.pal** - палитры цветов
- **.gry** - графика
- **.raw / .snd** - звуки

### 5.2 Конвертация

```cpp
// src/data/loader.h
class DataLoader {
public:
    static Sprite loadSprite(const std::string& path);
    static Map loadMap(const std::string& path);
    static Sound loadSound(const std::string& path);
    static Texture convertToModern(const LegacyAsset& asset);
    
private:
    static void parsePalette(const std::vector<uint8_t>& palData);
};
```

**Инструменты конвертации**:
- ImageMagick - для конвертации графики
- FFmpeg - для конвертации звука
- Custom tools - для специфических форматов GTA

### 5.3 Оптимизация

- Кэшировать сконвертированные ассеты
- Использовать компрессию текстур (WebP, ASTC)
- Lazy loading для больших уровней
- Mipmapping для масштабирования

---

## Фаза 6: Платформо-специфичные реализации

### 6.1 Windows
```cmake
# CMakeLists.txt
if(MSVC)
    set(PLATFORM_SOURCES src/platform/windows.cpp)
    target_link_libraries(dos_gta_recomp winmm) # Audio
endif()
```

- Использовать DirectSound опционально (для лучшей совместимости)
- Реализовать fullscreen/windowed переключение
- Поддержка XInput для Xbox контроллеров

### 6.2 macOS
```cmake
if(APPLE)
    set(PLATFORM_SOURCES src/platform/macos.mm)
    target_link_frameworks(dos_gta_recomp CoreAudio CoreFoundation)
endif()
```

- Metal рендеринг (альтернатива OpenGL)
- Поддержка Retina дисплеев
- Встроенное управление энергией

### 6.3 Linux
```cmake
if(UNIX AND NOT APPLE)
    set(PLATFORM_SOURCES src/platform/linux.cpp)
    # X11 / Wayland support
endif()
```

- Тестировать на основных дистрибутивах
- Использовать AppImage / Flatpak для распространения

### 6.4 Android
```cpp
// src/platform/android.cpp
// Использовать Android NDK + JNI
class AndroidPlatform {
public:
    static void initialize(ANativeActivity* activity);
    static int getScreenWidth();
    static int getScreenHeight();
    static bool isGamepadAvailable();
};
```

**Особенности**:
- Поддержка touch-экрана (дополнительно к геймпаду)
- Адаптивный UI для разных размеров экрана
- Обработка lifecycle приложения
- Использовать Android Game Development Kit (AGDK)

---

## Фаза 7: Новые фичи

### 7.1 Поддержка произвольных разрешений
```cpp
// src/core/config.h
struct GraphicsConfig {
    int width = 1920;
    int height = 1080;
    float aspectRatio = 16.0f / 9.0f;
    bool windowed = true;
    bool fullscreen = false;
    int vsync = 1;
    float fieldOfView = 60.0f;
};

// Масштабирование логики к разрешению
void Renderer::handleResolutionChange() {
    // Пересчитать projection matrix
    // Масштабировать UI
    // Адаптировать камеру
}
```

### 7.2 Сглаживание (Anti-Aliasing)

**FXAA** (Fast Approximate AA) - самый простой:
```glsl
// fragment.glsl
uniform sampler2D tex;
vec3 fxaa(sampler2D tex, vec2 uv) {
    // Post-processing фильтр
    // Обнаруживает края и сглаживает их
}
```

**MSAA** (Multisample AA) - встроенное в OpenGL:
```cpp
glEnable(GL_MULTISAMPLE);
glHint(GL_MULTISAMPLE_FILTER_HINT_NV, GL_NICEST);
```

### 7.3 Оконный режим
```cpp
// Переключение режимов
void Renderer::setFullscreen(bool fullscreen) {
    uint32_t flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    SDL_SetWindowFullscreen(window, flags);
    
    // Адаптировать разрешение
    if (fullscreen) {
        SDL_GetWindowSize(window, &width, &height);
    }
}
```

### 7.4 Поддержка геймпадов

```cpp
// src/input/gamepad.h
class GamepadManager {
public:
    void initialize();
    void update();
    
    GamepadState getState(int id);
    void setVibration(int id, float left, float right);
    void setLED(int id, uint8_t r, uint8_t g, uint8_t b); // PS5
    
    bool isConnected(int id) const;
    int getConnectedCount() const;
    
private:
    void onGamepadConnected(int id);
    void onGamepadDisconnected(int id);
    
    std::vector<SDL_GameController*> controllers;
};

struct GamepadState {
    float leftStickX, leftStickY;
    float rightStickX, rightStickY;
    float leftTrigger, rightTrigger;
    std::bitset<16> buttons; // A, B, X, Y, LB, RB, Menu, и т.д.
};
```

**Маппинг кнопок**:
```
GTA Actions          Xbox Controller     PlayStation Controller
Move Forward    -->  LStick Up     -->  LStick Up
Move Backward   -->  LStick Down   -->  LStick Down
Steer Left      -->  LStick Left   -->  LStick Left
Steer Right     -->  LStick Right  -->  LStick Right
Accelerate      -->  RT (Trigger)  -->  R2
Brake           -->  LT (Trigger)  -->  L2
Handbrake       -->  RB            -->  R1
Jump/Action     -->  A             -->  Cross (X)
Attack          -->  X             -->  Square
Menu            -->  Menu          -->  Options
Pause           -->  Start         -->  Start
```

### 7.5 Улучшенная графика (Optional)

- **Увеличение разрешения текстур** через апскейлинг (ESRGAN, Real-ESRGAN)
- **Динамическое освещение**
- **Тени** (Shadow mapping)
- **Частицы** (дым, взрывы, искры)
- **Post-processing эффекты** (bloom, color grading)

---

## Фаза 8: Build система и компиляция

### 8.1 CMakeLists.txt (основной)

```cmake
cmake_minimum_required(VERSION 3.16)
project(dos_gta_recomp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Зависимости
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenAL REQUIRED)

add_executable(dos_gta_recomp
    src/main.cpp
    src/core/game.cpp
    src/core/engine.cpp
    src/graphics/renderer.cpp
    src/graphics/shader.cpp
    src/graphics/texture.cpp
    src/input/input_manager.cpp
    src/input/gamepad.cpp
    src/audio/audio_manager.cpp
    src/data/loader.cpp
    src/game/world.cpp
    src/game/vehicle.cpp
    src/game/player.cpp
    src/game/ai.cpp
)

target_include_directories(dos_gta_recomp PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIRS}
    ${OPENAL_INCLUDE_DIR}
)

target_link_libraries(dos_gta_recomp
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${OPENAL_LIBRARY}
)

# Платформо-специфичные настройки
if(WIN32)
    target_sources(dos_gta_recomp PRIVATE src/platform/windows.cpp)
    target_link_libraries(dos_gta_recomp winmm)
elseif(APPLE)
    target_sources(dos_gta_recomp PRIVATE src/platform/macos.mm)
    target_link_frameworks(dos_gta_recomp CoreAudio CoreFoundation)
elseif(UNIX)
    target_sources(dos_gta_recomp PRIVATE src/platform/linux.cpp)
elseif(ANDROID)
    target_sources(dos_gta_recomp PRIVATE src/platform/android.cpp)
endif()
```

### 8.2 Установка зависимостей

**macOS (Homebrew)**:
```bash
brew install sdl2 glew openal-soft cmake
```

**Ubuntu/Debian**:
```bash
sudo apt-get install libsdl2-dev libglew-dev libopenal-dev cmake
```

**Windows (vcpkg)**:
```bash
vcpkg install sdl2:x64-windows glew:x64-windows openal-soft:x64-windows
```

**Android (NDK)**:
```bash
# Использовать Android Studio + NDK
# Или CMake с Android toolchain
```

### 8.3 Компиляция

```bash
# Unix-like (macOS, Linux)
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Windows (MSVC)
mkdir build && cd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%

# Android
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-24 ..
```

---

## Фаза 9: Тестирование

### 9.1 Unit тесты
```cpp
// tests/test_renderer.cpp
#include <catch2/catch.hpp>
#include "graphics/renderer.h"

TEST_CASE("Renderer initialization") {
    Renderer renderer(1920, 1080, true);
    REQUIRE(renderer.initialize() == true);
}
```

### 9.2 Функциональное тестирование

Чек-лист:
- [ ] Окно открывается на разных разрешениях
- [ ] AA включается/выключается без ошибок
- [ ] Геймпад распознается и работает
- [ ] Звук воспроизводится
- [ ] Окно/fullscreen переключается
- [ ] FPS стабилен (60+)
- [ ] Нет утечек памяти (valgrind, AddressSanitizer)

### 9.3 Кроссплатформенное тестирование

- **Windows**: тестировать на Windows 10/11
- **macOS**: Intel и Apple Silicon
- **Linux**: Ubuntu 20.04+, Fedora, Arch
- **Android**: минимум Android 8.0+, тестировать на реальных устройствах

---

## Фаза 10: Оптимизация и производительность

### 10.1 Профилирование

```cpp
// Использовать встроенный профайлер
class Timer {
public:
    void start(const std::string& name);
    void stop(const std::string& name);
    void printReport();
};
```

**Инструменты**:
- **Linux**: perf, valgrind
- **macOS**: Instruments (Xcode)
- **Windows**: Visual Studio Profiler, RenderDoc
- **Android**: Android Profiler (AS)

### 10.2 Оптимизация

- Использовать vertex buffers (VBO) вместо immediate mode
- Батчинг отрисовки (draw call reduction)
- LOD системы для дальних объектов
- Culling (frustum, occlusion)
- Кэширование шейдеров
- Ограничение FPS на мобильных (30/60 в зависимости от батареи)

---

## Фаза 11: Распространение

### 11.1 Windows
- Создать installer (NSIS, WiX)
- Распространять через Steam, itch.io, GitHub Releases

### 11.2 macOS
- Notarization для Gatekeeper
- Распространение через App Store или DMG

### 11.3 Linux
- AppImage для универсальности
- Поддержка основных пакетных менеджеров (Snap, Flatpak)

### 11.4 Android
- Подписать APK/AAB
- Распространять через Google Play или F-Droid

---

## Ресурсы и рекомендуемые библиотеки

| Компонент | Библиотека | URL |
|-----------|-----------|-----|
| Оконная система | SDL2 | https://www.libsdl.org/ |
| Графика | OpenGL + GLEW | https://www.opengl.org/ |
| Аудио | OpenAL-soft | https://openal-soft.org/ |
| Физика | Bullet3 / Rapier | https://pybullet.org/ |
| Математика | GLM | https://glm.g-truc.net/ |
| Логирование | spdlog | https://github.com/gabime/spdlog |
| JSON | nlohmann/json | https://github.com/nlohmann/json |
| Тестирование | Catch2 | https://github.com/catchorg/Catch2 |
| Reverse Engineering | Ghidra | https://ghidra-sre.org/ |

---

## Основные этапы реализации (Timeline)

1. **Неделя 1-2**: Анализ исходного кода, reverse engineering
2. **Неделя 3-4**: Базовый рендеринг, окно, OpenGL контекст
3. **Неделя 5-6**: Система ввода (клавиатура, мышь, геймпады)
4. **Неделя 7-8**: Аудио подсистема, физика
5. **Неделя 9-10**: Загрузка ассетов, конвертация
6. **Неделя 11-12**: Game loop, основная механика
7. **Неделя 13-14**: Новые фичи (AA, разрешения, оконный режим)
8. **Неделя 15-16**: Оптимизация, тестирование
9. **Неделя 17-18**: Платформо-специфичные доработки
10. **Неделя 19-20**: Build, packaging, распространение

---

## Замечания


- **Обратная совместимость**: Старые сохранения могут быть несовместимы
- **Баланс**: Поддержка новых разрешений может потребовать переделку UI/уровней
- **Производительность**: На мобильных устройствах может потребоваться снизить качество графики

---

## Следующие шаги

1. Распаковать ISO образ
2. Провести reverse engineering исходного кода
3. Создать структуру проекта (папки, CMakeLists.txt)
4. Начать с базовой окна и рендеринга
5. Постепенно добавлять компоненты

Успехов в реализации! 🚗
