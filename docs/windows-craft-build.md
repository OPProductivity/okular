# Build this Okular fork on Windows with Craft

This guide builds the source in this repository. KDE's official Okular download does not contain these changes. It describes a source build, not a redistributable installer or a byte-for-byte reproduction of a previous binary. A fresh Craft installation on a second PC still needs to be tested; Craft and its dependency cache change over time.

## 1. Install the tools

Use 64-bit Windows 10 or 11. Install Git, Python 3, and Visual Studio 2022 (Community or Build Tools) with **Desktop development with C++** and the Windows SDK. Install standalone CMake if it is not already available. Enable Windows Developer Mode. Follow KDE's [current Craft setup instructions](https://develop.kde.org/docs/getting-started/building/craft/) for Python certificates, the PowerShell execution policy, and Craft bootstrap. Choose an x64 MSVC 2022 configuration and a short Craft path such as `C:\CraftRoot`. Install Craft's `jom` build tool if it is not present at `C:\CraftRoot\dev-utils\bin\jom.exe`.

Use the **Developer PowerShell for VS 2022** shortcut for the remaining commands. It makes MSVC and the Windows SDK available. If Craft was installed elsewhere, replace `C:\CraftRoot` in every command below with that root. If a command reports a missing tool, check it before proceeding:

```powershell
git --version
python --version
cmake --version
Get-Command cl.exe -ErrorAction SilentlyContinue
Test-Path C:\CraftRoot\dev-utils\bin\jom.exe
```

Craft's installer and options may change; the KDE guide is authoritative for bootstrap. Do not copy another person's `CraftSettings.ini`, local configuration, or application data into the new installation.

## 2. Check out the fork

Choose a source directory outside `C:\CraftRoot` (the example uses a short path). To rebuild a particular known source revision, check out its commit; otherwise stay on `master` for the newest code.

```powershell
New-Item -ItemType Directory -Force C:\src | Out-Null
git clone https://github.com/OPProductivity/okular.git C:\src\okular
Set-Location C:\src\okular
git rev-parse HEAD
# Optional: git checkout 260e8390143b0a7a31434a61b348f5f390569058
```

The example commit is the source revision validated in October 2026. Later commits may include documentation or fixes. Keep a record of the exact revision you build. The checked environment used Craft commit `ddb5c39fac12a3341aaf2038e784450f315842ee`, KDE blueprint commit `8b7b096bd22b9d34858809c34e99d0a995a7b216`, MSVC toolset directory `14.44.35207`, CMake `4.4.4`, and jom `1.1.3`. These identify the reference environment, but the installed Craft dependency packages were not frozen in a lockfile.

## 3. Prepare Craft and build the dependencies

This fork was last rebuilt with MSVC 2022 x64, NMake Makefiles, and `jom`. In `C:\CraftRoot\etc\CraftSettings.ini`, set these entries under the existing `[Compile]` section (keep the other settings made by Craft):

```ini
[Compile]
UseNinja = False
MakeProgram = jom
```

Start a new Developer PowerShell after editing the file. Enter Craft's environment and confirm the source directory and build tool:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
Test-Path C:\CraftRoot\dev-utils\bin\jom.exe
Set-Location C:\src\okular
craft --ignoreInstalled --options "kde/applications/okular.srcDir=$((Get-Location).Path)" kde/applications/okular
```

Use the full `kde/applications/okular` blueprint name. The `srcDir` option ensures that Craft builds this checkout. The initial command also obtains Okular's Qt/KDE dependencies. It may take a long time and several gigabytes. A build from KDE's upstream repository or without this `srcDir` option would omit this fork's changes. If Craft reports a Python blueprint problem, check its currently supported Python versions first. The reference environment had local Craft fixes, described below; apply them only if a fresh Craft still has the same problem.

### Reference Craft Python workaround (only if needed)

The reference Craft checkout set `[libs/python] ignored = True` in `C:\CraftRoot\etc\BlueprintSettings.ini` and used system/virtual-environment Python. Its Craft source also changed `blueprints\libs\python\python.py` from an unavailable `defaultTarget = "3.15.6"` to an available `"3.14.6"`. In `bin\BuildSystem\PipBuildSystem.py`, both checks for an active Craft Python were changed to account for `craftPython.isIgnored()`: the `python()` check uses `if not craftPython.categoryInfo.isActive or craftPython.isIgnored():`, and `usesCraftPython` uses `craftPython.categoryInfo.isActive and not craftPython.isIgnored()`. These edits are outside this Okular repository. A newer Craft may already have fixed the mismatch; inspect the installed blueprint and error before changing it. Back up the Craft files before any local edit, and expect updates to overwrite local Craft changes.

## 4. Repeat the checked NMake build and tests

The maintainer's October 2026 build used a separate NMake build directory after Craft had populated `C:\CraftRoot`. The commands below express that build without any machine-specific user path. They run from a Developer PowerShell with Craft's environment active. Use the CMake executable installed on your machine.

```powershell
$craftRoot = 'C:\CraftRoot'
$source = (Get-Location).Path
$build = Join-Path $craftRoot 'build\kde\applications\okular\work\build-nmake'
$env:PATH = "$craftRoot\bin;$craftRoot\dev-utils\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$craftRoot\plugins"
cmake -S $source -B $build -G 'NMake Makefiles' `
  "-DCMAKE_MAKE_PROGRAM=$craftRoot/dev-utils/bin/jom.exe" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  "-DCMAKE_INSTALL_PREFIX=$craftRoot" "-DCMAKE_PREFIX_PATH=$craftRoot" `
  -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON -DBUILD_WITH_QT6=ON
cmake --build $build --parallel 4
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir $build --output-on-failure
Remove-Item Env:QT_QPA_PLATFORM
```

The offscreen setting is only for tests. The earlier checked build registered 32 tests, but the count can change with source/dependency revisions. A passing build does not by itself prove File Explorer routing, theme appearance, or tab behavior on another Windows desktop.

The `cmake --build` step above does **not** install its output into the live Craft runtime. To install that newly compiled output after closing Okular, use `cmake --install $build` from the same environment. Then confirm the installed executable and plugin were updated. Keep existing user settings and documents out of the build directory.

## 5. Run and check the built reader

```powershell
Test-Path C:\CraftRoot\bin\okular.exe
Test-Path C:\CraftRoot\plugins\kf6\parts\okularpart.dll
& C:\CraftRoot\bin\okular.exe --version
& C:\CraftRoot\bin\okular.exe
```

Open two local PDFs and verify that they appear in one window as tabs, the `+` button works, and the page counter stays in place. Close the reader, reopen it without a filename, and check session restore. Verify Breeze Light and Breeze Darker on the new PC. To use the build from Explorer, select this exact `okular.exe` through **Open with > Choose another app > Choose an app on your PC**, then set it as the default if desired. Check shortcuts and taskbar pins separately; they can still launch a different Okular.

Do not remove `C:\CraftRoot` while running this build from it. Its `bin`, `lib`, `plugins`, and `share` directories provide the application and runtime dependencies. Its `build` directory contains build trees and can be recreated, but deleting it makes the next build slower and removes incremental build state. A source checkout alone is not an installed PDF reader.

## Reproducibility and privacy

The public Git repository preserves this fork's source and these instructions. It does not preserve a frozen Visual Studio toolchain, all Craft blueprints/cache packages, an installer, per-user settings, session backups, or personal documents. For the closest functional rebuild, record the source commit and the Craft/Visual Studio/CMake versions used, and retain a tested installer or runtime backup separately if you require rollback without rebuilding. Exact binary hashes may differ on another PC even when functionality matches.

Do not publish `C:\CraftRoot\etc`, CMake caches, build logs, `%LOCALAPPDATA%\okular`, `%LOCALAPPDATA%\okularrc`, or local PDF files. These can contain usernames, document paths, session history, or machine details. The repository's public URL identifies its hosting account; source documentation cannot make that account anonymous.
