# CS2 Skin Changer (внутренний DLL, ImGui, D3D11)

Внутренний модуль: хукает `IDXGISwapChain::Present` / `ResizeBuffers` для меню и `FrameStageNotify` для записи скинов. Меню по `INSERT`.

## Что уже работает без каких-либо данных об игре
- Меню (Skins / Status / Settings), логотип, профиль `profile.ini` (автосохранение при закрытии меню).
- Чтение `offsets.ini`, строгая валидация, страница Status показывает, какой именно «замок» ещё закрыт.
- Загрузка `skins.tsv` (оружие и финиши), поиск и выбор.

## Что закрыто, пока ты не дашь значения
Запись в игру включается только когда одновременно выполнено всё:
1. все обязательные ключи в `offsets.ini` заполнены, раскладка прошла проверку согласованности;
2. `client_time_date_stamp` и `client_size_of_image` совпали с реальным `client.dll` (иначе сигнатуры даже не сканируются);
3. `sig_frame_stage_notify` найдена ровно в одном месте;
4. заданы `frame_stage_apply` и `confirm_fsn_prototype=1`.

Ни одного значения оффсета, сигнатуры, индекса стадии или прототипа я не придумывал. Всё это поля в `offsets.ini.example`, пустые.

## Сборка
Требования: Visual Studio 2022 (MSVC, x64), CMake 3.24+, доступ к GitHub (ImGui v1.91.5 и MinHook v1.3.4 тянутся через FetchContent; если тег не найден, поправь его в `CMakeLists.txt`).

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Результат: `build/Release/skinchanger.dll`. Рядом положи `offsets.ini`, `skins.tsv` (профиль и лог создаются сами).

Проверки без игры (любая ОС):
```
g++ -std=c++20 -Wall -Wextra -Isrc tests/core_tests.cpp -o core_tests && ./core_tests
python tools/gen_skins.py --selftest
```

## Логотип
`assets/logo.svg` -> `src/assets/logo_png.hpp` через `python tools/embed_logo.py` (нужен `rsvg-convert`). Если хочешь конкретную иконку с iconsvg.xyz, вставь её SVG в `assets/logo.svg` и запусти скрипт.

## skins.tsv
Извлеки из VPK (Source2Viewer/VRF) `scripts/items/items_game.txt` и `resource/csgo_english.txt`:
```
python tools/gen_skins.py --items_game items_game.txt --language csgo_english.txt -o skins.tsv
```
Скрипт предполагает структуру `items_game/items/<def>/name` и `items_game/paint_kits/<id>/name`; если у твоей сборки иначе, он остановится с понятной ошибкой, а не запишет мусор.

## Как найти недостающее (рецепты)
- **Build identity**: PE-заголовок `client.dll` на диске (System Informer / PE-bear): TimeDateStamp и SizeOfImage.
- **dwEntityList / dwLocalPlayerPawn / dwLocalPlayerController и поля**: свой cs2-dumper для этой сборки; поля из схемных классов, указанных в комментариях `offsets.ini.example`.
- **Константы системы энтити** (`handle_*`, `entlist_chunk_base`, `entity_stride`, `identity_handle_off`, `utl_vector_*`): в ReClass на живом `dwEntityList` проверь разбиение хендла из `m_hMyWeapons` на чанк и слот и размер слота. Проверяй, пока `GetBaseEntity`-подобная цепочка не даст оружие с нужным `m_iItemDefinitionIndex`.
- **FrameStageNotify**: функция клиента, которая получает номер стадии кадра. Найди через xref на строки стадий/сети либо через интерфейс клиента (`CreateInterface` в `client.dll`), проверь, что это `void(this, int stage)`, построй уникальную сигнатуру (не менее 4 конкретных байт, ровно одно совпадение). Поставь `confirm_fsn_prototype=1` только после этого.
- **frame_stage_apply**: номер стадии, на которой запись безопасна. Временно залогируй значения `stage` за кадр в хуке и выбери ту, что идёт после обновления сетевых данных; проверь экспериментом.
- **sig_regenerate_weapon_skin** (необязательно): функция, пересобирающая визуал оружия после смены fallback-значений. Без неё скин может появиться только после переэкипировки оружия.

## Известные ограничения
- Поддерживается только D3D11 (Vulkan-режим CS2 хук меню не подхватит, в логе будет причина).
- Сканер сигнатур пропускает нечитаемые чанки образа: в худшем случае ложное «не найдено», но не запись по неверному адресу.
- Пока меню открыто, мышь продолжает двигать камеру в игре (ввод игры идёт через raw input, не через WndProc).
- `Disable and unhook` отключает хуки, но DLL остаётся в памяти намеренно.

## Сборка на GitHub Actions (без установки Visual Studio)
1. Создай репозиторий на github.com (лучше Private) и залей проект: `git init`, `git add .`, `git commit -m "init"`, `git branch -M main`, `git remote add origin <URL>`, `git push -u origin main`.
2. Вкладка **Actions** -> workflow `build` стартует сам при каждом push (или кнопка **Run workflow**).
3. Когда прогон зелёный, открой его и внизу в **Artifacts** скачай `cs2-skinchanger-x64` (внутри `skinchanger.dll`, `offsets.ini.example`, README, `tools/`).
Workflow: `.github/workflows/build.yml`, образ `windows-2022` (VS 2022), перед сборкой прогоняет самотест генератора, после сборки запускает `core_tests.exe`.
