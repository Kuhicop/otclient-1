<h1>
  <img src="https://github.com/mehah/otclient/blob/main/data/images/clienticon.png?raw=true" width="32" alt="logo"/>
  ADREAM Client (mehah/otclient – Tibia 7.6)
</h1>

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## Compatibilidades

| <br />Protocol / version       | Description                    | Required Feature                                                                                                                                                                                                                                                     | Compatibility |
| ------------------------------ | ------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------- |
| TFS (7.72)                     | Downgrade nekiro / Nostalrius  | [force-new-walking-formula: true](https://github.com/mehah/otclient/blob/cf7badda978de88cb3724615688e3d9da2ff4207/data/setup.otml#L21) • [item-ticks-per-frame: 500](https://github.com/mehah/otclient/blob/cf7badda978de88cb3724615688e3d9da2ff4207/data/setup.otml#L32) | ✅            |
| TFS 0.4 (8.6)                  | Fir3element                    | [item-ticks-per-frame: 500](https://github.com/mehah/otclient/blob/cf7badda978de88cb3724615688e3d9da2ff4207/data/setup.otml#L32)                                                                                                                                        | ✅            |
| TFS 1.5 (8.0 / 8.60)           | Downgrade nekiro / MillhioreBT | [force-new-walking-formula: true](https://github.com/mehah/otclient/blob/cf7badda978de88cb3724615688e3d9da2ff4207/data/setup.otml#L21) • [item-ticks-per-frame: 500](https://github.com/mehah/otclient/blob/cf7badda978de88cb3724615688e3d9da2ff4207/data/setup.otml#L32) | ✅            |
| TFS 1.4.2 (10.98)              | Release Otland                 |                                                                                                                                                                                                                                                                      | ✅            |
| TFS 1.6 (13.10)                | Main repo otland (2024)        | [See wiki](https://github.com/mehah/otclient/wiki/Tutorial-to-Use-OTC-in-TFS-main)                                                                                                                                                                                      | ✅            |
| Canary (13.21 / 13.32 / 13.40) | OpenTibiaBr                    | [See Wiki](https://docs.opentibiabr.com/opentibiabr/projects/otclient-redemption/about#how-to-connect-on-canary-with-otclient-redemption)                                                                                                                               | ✅            |
| Canary (14.00 ~ 14.12)         | OpenTibiaBr                    | [See Wiki](https://docs.opentibiabr.com/opentibiabr/projects/otclient-redemption/about#how-to-connect-on-canary-with-otclient-redemption)                                                                                                                               | ✅            |
| Canary (15.00 ~ 15.10)         | OpenTibiaBr                    | [See Wiki](https://docs.opentibiabr.com/opentibiabr/projects/otclient-redemption/about#how-to-connect-on-canary-with-otclient-redemption)                                                                                                                               | ❌            |

## Requisitos

- Windows 10/11 x64
- Visual Studio 2022 (Desktop C++)
- Git
- vcpkg

## Preparar vcpkg

cd C:\Users\Adrian\Desktop\OTSERV
git clone https://github.com/microsoft/vcpkg
cd vcpkg
bootstrap-vcpkg.bat

## Compilar cliente

Abrir: x64 Native Tools Command Prompt for VS 2022

set VCPKG_ROOT=C:\Users\Adrian\Desktop\OTSERV\vcpkg
cd C:\Users\Adrian\Desktop\OTSERV\ADREAM\mehah-otclient

mkdir build
cmake -S . -B build -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

cmake --build build --config Release --target otclient

## Ejecutable

where /r build otclient.exe

## Cliente 7.6

Crear:
data\things\760\

Copiar:

- Tibia.dat
- Tibia.spr
