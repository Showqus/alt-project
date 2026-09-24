# BedrockQoL: AutoSprint и Zoom для Minecraft Bedrock (Windows) 1.21.51

DLL-мод для **Minecraft for Windows (Bedrock Edition) 1.21.51** (подходит для всей ветки 1.21.5x).
Добавляет две функции:

| Функция | Управление по умолчанию | Что делает |
|---|---|---|
| **AutoSprint** | включён сразу, **F8** включает/выключает | Пока вы держите **W**, игра считает, что клавиша бега (**Ctrl**) тоже зажата, поэтому персонаж бежит без отдельного нажатия. |
| **Zoom** | держите **C** | Приближение в 4 раза с плавной анимацией. **Колесо мыши** во время зума меняет силу приближения (хотбар при этом не прокручивается). Рука в первом лице не увеличивается. |
| Выгрузка | **END** | Снимает все хуки и выгружает DLL из игры без перезапуска. |

Все клавиши и параметры настраиваются в `config.ini` (см. ниже).

## Быстрый старт

1. Скачайте из папки [`release/`](release/) два файла, `BedrockQoL.dll` и `BedrockQoLInjector.exe`, и положите их в одну папку.
2. Запустите Minecraft 1.21.51.
3. Запустите `BedrockQoLInjector.exe`. Он сам:
   * выдаст файлу DLL права **ALL APPLICATION PACKAGES**, без которых UWP-версия игры не может загрузить DLL;
   * найдёт процесс `Minecraft.Windows.exe` (если игра ещё не запущена, подождёт её);
   * загрузит `BedrockQoL.dll` в игру.
4. Зайдите в мир: держите **W**, чтобы бежать, и **C**, чтобы приблизить.

Вместо встроенного инжектора можно использовать любой другой (Fate Injector и т.п.). Если он не выдаёт права сам, выполните:
```
icacls BedrockQoL.dll /grant *S-1-15-2-1:(RX)
```

> Антивирус может ругаться на инжектор: так он реагирует на любую программу, которая загружает DLL в другой процесс.
> Исходный код обоих файлов лежит в этом репозитории, их можно собрать самостоятельно (см. «Сборка»).

## Настройки (`config.ini`)

При первом запуске мод создаёт файл настроек и лог здесь:
```
%LOCALAPPDATA%\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\RoamingState\BedrockQoL\config.ini
%LOCALAPPDATA%\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\RoamingState\BedrockQoL\BedrockQoL.log
```
После изменения настроек нажмите в игре **END** и снова запустите инжектор (или перезапустите игру).

| Секция / ключ | По умолчанию | Описание |
|---|---|---|
| `[General] UnloadKey` | `END` | Клавиша выгрузки мода |
| `[General] RequireHiddenCursor` | `1` | Функции работают только когда курсор скрыт, то есть вы в мире, а не в чате, инвентаре или меню. |
| `[AutoSprint] Enabled` | `1` | Включён ли AutoSprint при старте |
| `[AutoSprint] ToggleKey` | `F8` | Вкл/выкл AutoSprint (`NONE` = без клавиши) |
| `[AutoSprint] ForwardKey` | `W` | Должна совпадать с «Вперёд» в настройках игры (для AZERTY: `Z`) |
| `[AutoSprint] SprintKey` | `CTRL` | Должна совпадать с «Бег» в настройках игры |
| `[AutoSprint] FallbackSendInput` | `1` | Запасной режим через `SendInput`, если хук клавиатуры не установился |
| `[Zoom] Key` | `C` | Клавиша зума |
| `[Zoom] Toggle` | `0` | `0` = держать, `1` = нажать для вкл/выкл |
| `[Zoom] Factor` | `4.0` | Во сколько раз уменьшается FOV |
| `[Zoom] MinFactor` / `MaxFactor` | `1.5` / `50` | Пределы для колеса мыши |
| `[Zoom] ScrollAdjust` / `ScrollStep` | `1` / `1.25` | Регулировка колесом и шаг (множитель за одно деление колеса) |
| `[Zoom] RememberScroll` | `0` | `1` = запоминать уровень зума, выставленный колесом |
| `[Zoom] Smooth` / `SmoothSpeed` | `1` / `12` | Плавная анимация и её скорость |
| `[Zoom] ZoomHand` | `0` | `1` = увеличивать и руку тоже |
| `[Signatures] GetFov`, `KeyboardFeed`, `MouseFeed` | пусто | Свои сигнатуры, если после обновления игры встроенные перестанут находиться |

Имена клавиш: `A`–`Z`, `0`–`9`, `F1`–`F24`, `CTRL`, `SHIFT`, `ALT`, `SPACE`, `TAB`, `CAPSLOCK`, `END`, `HOME`, `INSERT`, `DELETE`, `PAGEUP`, `PAGEDOWN`, `NUMPAD0`–`NUMPAD9`, `NONE` или код клавиши (`0x43`).

## Если что-то не работает

Откройте `BedrockQoL.log`, в нём всё написано:

* **`signature not found`**: у вас другая версия игры, и функцию не удалось найти. Мод рассчитан на 1.21.5x. В логе также указана версия пакета игры (`Microsoft.MinecraftUWP_1.21.5101.0_...`).
* **`Zoom key ignored: the mouse cursor is visible`** в мире: поставьте `RequireHiddenCursor=0`.
* **Бег не включается**: проверьте, что `ForwardKey` и `SprintKey` совпадают с вашими клавишами в игре. Если в настройках управления включён режим переключения бега (Toggle Sprint), выключите его.
* **`Keyboard hook receives no events - switching to the polling fallback`**: хук клавиатуры не получает события, поэтому мод перешёл на опрос клавиш через `GetAsyncKeyState` и `SendInput`. Функции должны продолжить работать.

## Как это устроено

* `src/scanner.cpp`: поиск функций игры по байтовым сигнатурам в секции `.text` `Minecraft.Windows.exe`.
* `src/hooks.cpp`: хуки через [MinHook](https://github.com/TsudaKageyu/minhook) на три функции игры:
  * `LevelRendererPlayer::getFov` → Zoom: итоговый FOV умножается на `1/Factor` с плавной анимацией;
  * `Keyboard::feed` → AutoSprint (подаёт игре нажатие клавиши бега, пока зажата клавиша «вперёд») и горячие клавиши;
  * `MouseDevice::feed` → колесо мыши во время зума.
* Сигнатуры для 1.21.5x взяты из открытого клиента [Flarial](https://github.com/flarialmc/dll) (`SigInit.cpp`); там они используются для версий 1.20.x–1.21.11x.
* `src/features/autosprint.cpp`, `src/features/zoom.cpp`: логика функций.
* `injector/injector.cpp`: инжектор (`LoadLibraryW` + `CreateRemoteThread`, выдача ACL для UWP).

## Сборка

**Windows, Visual Studio 2022 + CMake:**
```
cmake -B build -A x64
cmake --build build --config Release
```
Результат: `build/Release/BedrockQoL.dll` и `build/Release/BedrockQoLInjector.exe`.

**Linux, кросс-компиляция MinGW-w64:**
```
sudo apt install mingw-w64 cmake ninja-build
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build
```

**Тест** (только для MinGW): `tests/fake_game.cpp` собирается в «поддельную игру», в коде которой есть функции с теми же сигнатурами, что и у 1.21.5x. Тест загружает DLL и проверяет поиск сигнатур, хуки, зум, колесо мыши, AutoSprint и выгрузку:
```
cd build && wine64 fake_game.exe BedrockQoL.dll     # или просто fake_game.exe на Windows
```

## Важно

* Мод проверен тестом на «поддельной игре» (Windows API через Wine). В настоящей Minecraft 1.21.51 он не запускался. Сигнатуры взяты из клиента, который поддерживал эту версию.
* Чувствительность мыши во время зума не снижается: для этого нужен отдельный хук.
* Zoom и Toggle/Auto Sprint есть во многих легальных клиентах, но на некоторых серверах любые сторонние модификации запрещены. Проверяйте правила сервера.
* Лицензия MinHook: BSD 2-Clause (`third_party/minhook/LICENSE.txt`).
