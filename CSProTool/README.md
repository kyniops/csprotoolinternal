# CSProTool

Base interne CS2 propre, réécrite à partir de l’ancien projet **CS2-Internal-Cheat**, avec une architecture claire inspirée des bases open source (hook D3D11 + ImGui + offsets `cs2-dumper`).

## Stack

- C++17 / Visual Studio 2022+
- Kiero + MinHook (Present DXGI)
- Dear ImGui (DX11 / Win32) stock, non forké
- Offsets / schemas : [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper) (`generated/`, dump 2026-08-13)

## Features

- ESP : box, health, name, skeleton, snaplines, glow, FOV circle
- Aimbot : FOV, smooth, team check, touche Left Alt
- Menu ImGui (Insert) / unload (End)

## Build

1. Ouvrir `CSProTool.sln`
2. Config **Release | x64**
3. Build → `bin\x64\Release\CSProTool.dll`

Ou en CLI :

```bat
msbuild CSProTool.sln /p:Configuration=Release /p:Platform=x64
```

## Injection

Injecter `CSProTool.dll` dans `cs2.exe` (LoadLibrary ou manual map), une fois en menu / en partie.

## Update offsets

Quand CS2 update :

1. Lancer [cs2-dumper](https://github.com/a2x/cs2-dumper) pendant que le jeu tourne
2. Remplacer `generated/offsets.hpp` et `generated/client_dll.hpp`
3. Rebuild

## Structure

```
CSProTool/
  generated/     # dumps cs2-dumper
  vendor/        # imgui, kiero, minhook
  src/
    core/        # memory, modules
    sdk/         # math, schema, entities
    features/    # esp, aimbot, config
    hooks/       # present
    menu/
    dllmain.cpp
```

## Avertissement

Usage éducatif / privé uniquement. Tricher en ligne viole les CGU Valve et peut entraîner un ban VAC. Tu assumes les risques.