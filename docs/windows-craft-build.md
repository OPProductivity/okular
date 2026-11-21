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

The example commit is the source revision validated in October 2026. Later commits may include documentation or fixes. Keep a record of the exact revision you build. The earlier checked environment used Craft commit `ddb5c39fac12a3341aaf2038e784450f315842ee` and KDE blueprint commit `8b7b096bd22b9d34858809c34e99d0a995a7b216`. These are historical reference commits, not pins for the current installation. The 21 November 2026 local recovery profile used Okular source commit `976904a0dfc35bab4d152760b67ff4bfe3f1a32b`, Craft commit `0e96fbc37aa76adef5b5e60d10ccf9cd477a2667`, KDE blueprint commit `5e61dd7ef449b7680551cca8a0a98a8fcde4a20b`, MSVC toolset directory `14.44.35207`, CMake `4.4.4`, and jom `1.1.3`. Craft and the blueprint checkout also had local recipe and Python changes; their commit IDs alone do not describe the environment. The installed Craft dependency packages were not frozen in a public lockfile.

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

The [PDF dependency recipe overrides](craft-overrides/README.md) preserve the
local recipe changes used to build Poppler 26.11.0 and libspectre 0.2.12 with
NMake/jom. They are optional reference patches. On 17 November 2026 both were
merged through Craft, Okular was rebuilt against them, and its 32 registered
tests plus an installed-library offscreen shell check passed. A foreground
visual check remains separate from these automated checks.

### Keep Qt Text-to-Speech aligned

When updating Qt Core, rebuild Text-to-Speech to the same Qt patch version before
accepting an Okular installation. On MSVC, install Visual Studio's signed
`Microsoft.VisualStudio.Component.VC.ATL` component so the Windows SAPI backend
can build. Check that `atlbase.h` is present in the selected MSVC toolset. The
17 November 2026 Qt 6.11.3 Speech build needed Microsoft NMake: `jom` rejected
a generated NMake rule, while `nmake` compiled the module and its SAPI and WinRT
plugins successfully. Use an NMake build tree; back up an existing Ninja tree
before configuring a different generator.

For an already fetched and unpacked Qt 6.11.3 source in Craft, the focused
build and staging commands are:

```powershell
craft -i --target 6.11.3 --options '[Compile]MakeProgram=nmake' --compile libs/qt6/qtspeech
craft -i --target 6.11.3 --options '[Compile]MakeProgram=nmake' --install libs/qt6/qtspeech
```

The second command writes Craft's package image. Before `qmerge`, close the
running reader, back up the installed Speech package and verify the staged
image contains `Qt6TextToSpeech.dll`, `qtexttospeech_sapi.dll`, and
`qtexttospeech_winrt.dll`. Then merge with
`craft -i --target 6.11.3 --qmerge libs/qt6/qtspeech` and run the loader and
document-opening checks below. Use one matching Qt version throughout; do not
blindly apply 6.11.3 to a future Craft installation that has a different Qt
version.

### Reference Craft Python workaround (only if needed)

The reference Craft checkout set `[libs/python] ignored = True` in `C:\CraftRoot\etc\BlueprintSettings.ini` and used system/virtual-environment Python. Its local Craft blueprint changed `blueprints\libs\python\python.py` from `defaultTarget = "3.12.13"` to `"3.14.6"`; this is a blueprint target, not the version of the system Python used to run Craft. In `bin\BuildSystem\PipBuildSystem.py`, both checks for an active Craft Python were changed to account for `craftPython.isIgnored()`: the `python()` check uses `if not craftPython.categoryInfo.isActive or craftPython.isIgnored():`, and `usesCraftPython` uses `craftPython.categoryInfo.isActive and not craftPython.isIgnored()`. The exact local diff is preserved in [the Craft override patches](craft-overrides/README.md). A newer Craft may already have fixed the mismatch; inspect the installed code and error before applying it. Keep local settings private and expect Craft updates to overwrite local changes.

### Reference dependency and feature profile

The 21 November 2026 installed package database recorded QtBase `6.11.3-1`, most Qt 6 modules `6.11.3` (Qt Image Formats and Qt Translations `6.11.1`), KDE Frameworks mostly `6.27.0`, Poppler `26.11.0`, libspectre `0.2.12`, and Ghostscript `10.08.0`. These are the observed reference versions, not a lockfile or a guarantee that a later Craft cache still serves the same packages. The complete 202-package manifest and installed-file hashes are private because they record a specific machine's state.

The two [sanitized reference settings files](craft-overrides/README.md) preserve the exact non-sensitive values that influenced this profile. They include the x64 MSVC ABI, RelWithDebInfo/NMake/jom settings, the ignored Ninja/Craft Python packages, and QtBase, Declarative, Poppler and other blueprint options. They are reference excerpts, not complete settings files or universal defaults; inspect the available options in the pinned Craft/blueprint versions before merging them. The reference machine had automatic daily blueprint updates enabled, so recheck the actual checkout revisions before a build. Do not publish or copy the entire local settings files, which may contain paths or private values.

For the closest rebuild on another PC, bootstrap Craft from KDE's current Windows instructions, start from the public Craft parent commit `1ef5a8fd0c8a53c3afd062a4d229a92e161b93f9` and public KDE blueprint commit `5e61dd7ef449b7680551cca8a0a98a8fcde4a20b`, then follow [the reviewed patch sequence](craft-overrides/README.md). The reference Craft commit `0e96fbc37` is local and cannot be fetched from KDE; its only committed code change is preserved as a patch here. Set a compatible x64 MSVC/NMake profile and build this fork using `srcDir`. Verify the Craft and blueprint code state again before building, since automatic updates can change it. Record the resolved package versions and Qt configure summary, then run the app and loader checks below. This sequence is documented but has **not** been completed on a second PC; a new Craft environment may require adapting patches or package versions.

## 4. Repeat the checked NMake build and tests

The maintainer's current build uses Craft's retained `work/build` directory, regenerated with NMake/jom. The earlier separate `build-nmake` directory was removed. These commands run from a Developer PowerShell with Craft's environment active. Use the CMake executable installed on your machine.

```powershell
$craftRoot = 'C:\CraftRoot'
$source = (Get-Location).Path
$build = Join-Path $craftRoot 'build\kde\applications\okular\work\build'
$env:PATH = "$craftRoot\bin;$craftRoot\dev-utils\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$craftRoot\plugins"
cmake -S $source -B $build -G 'NMake Makefiles' `
  "-DCMAKE_MAKE_PROGRAM=$craftRoot/dev-utils/bin/jom.exe" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  "-DCMAKE_INSTALL_PREFIX=$craftRoot" "-DCMAKE_PREFIX_PATH=$craftRoot" `
  -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON -DBUILD_WITH_QT6=ON
cmake --build $build
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir $build --output-on-failure
Remove-Item Env:QT_QPA_PLATFORM
```

On Windows, `mainshelltest.exe` and `parttest.exe` are compiled but intentionally excluded from the regular CTest suite because some legacy cases are unstable. Run relevant shell cases directly under `QT_QPA_PLATFORM=offscreen`, capture results with Qt Test's `-o <path>,txt`, and wait for the GUI-subsystem process to exit before reading that file. The test binary enables Qt's test paths. Do not set `OKULAR_TEST_INSTANCE` for the parent shell test process: several cases read its test-mode shared config and would otherwise check a different store. For example, run `mainshelltest.exe testTemporarilyMissingTabSurvivesRestore -o results.txt,txt` from the build's `bin` directory. Record the Git revision, case names, exit status, and reported pass/fail totals.

The offscreen setting is only for tests. The earlier checked build registered 32 tests, but the count can change with source/dependency revisions. A passing build does not by itself prove File Explorer routing, theme appearance, or tab behavior on another Windows desktop.

The `cmake --build` step above does **not** install its output into the live Craft runtime. To install that newly compiled output after closing Okular, use `cmake --install $build` from the same environment. Then confirm the installed executable and plugin were updated. Keep existing user settings and documents out of the build directory.

## 5. Run and check the built reader

```powershell
Test-Path C:\CraftRoot\bin\okular.exe
Test-Path C:\CraftRoot\plugins\kf6\parts\okularpart.dll
& C:\CraftRoot\bin\okular.exe --version
& C:\CraftRoot\bin\okular.exe
```

The `--version` check does not load Okular's document-window part. After a Qt
dependency update, also check that the installed part loads, then open a
disposable PDF and run the registered and relevant shell tests:

```powershell
python -c "import ctypes,os; os.add_dll_directory(r'C:\CraftRoot\bin'); ctypes.WinDLL(r'C:\CraftRoot\bin\okularpart.dll'); print('Okular part loads')"
```

Keep the Qt modules at compatible patch levels. An older Qt Text-to-Speech DLL
can prevent the part from loading even when `okular.exe --version` succeeds.
Check the exact missing DLL or entry point before rebuilding or replacing
dependencies. Preserve the SAPI and WinRT speech plugins when updating that
module.

Open two local PDFs and verify that they appear in one window as tabs, the `+` button works, and the page counter stays in place. Close the reader, reopen it without a filename, and check session restore. Verify Breeze Light and Breeze Darker on the new PC. To use the build from Explorer, select this exact `okular.exe` through **Open with > Choose another app > Choose an app on your PC**, then set it as the default if desired. Check shortcuts and taskbar pins separately; they can still launch a different Okular.

Do not remove `C:\CraftRoot` while running this build from it. Its `bin`, `lib`, `plugins`, and `share` directories provide the application and runtime dependencies. Its `build` directory contains build trees and can be recreated, but deleting it makes the next build slower and removes incremental build state. A source checkout alone is not an installed PDF reader.

## Reproducibility and privacy

Before retiring dependency build output, use the
[Craft recovery and build-retirement workflow](windows-craft-recovery.md).
It preserves actual patched sources, local Craft/blueprint changes and installed
runtime hashes, and provides fresh NMake app/add-on-module probes without
installing, launching or deleting anything. A `build` directory is not uniformly
disposable: keep the current Okular incremental build and source/staging inputs.

The public Git repository preserves this fork's source and these instructions. It does not preserve a frozen Visual Studio toolchain, all Craft blueprints/cache packages, an installer, per-user settings, session backups, or personal documents. For the closest functional rebuild, record the source commit and the Craft/Visual Studio/CMake versions used, and retain a tested installer or runtime backup separately if you require rollback without rebuilding. Exact binary hashes may differ on another PC even when functionality matches.

### What the rebuild checks establish

The November 2026 cold Qt module probes used retained Qt 6.11.3 source trees and the **already installed** Craft prefix. The fresh Okular probe also linked against that prefix. Successful compilation therefore demonstrates rebuilding with those dependencies available; it does not demonstrate bootstrapping the entire dependency graph or reproducing the installed binaries byte for byte. Matching `FEATURE_` cache entries is a useful profile check, but it does not compare every compiler option, plugin, or runtime behavior. QtBase needed a corrected reconfigure in its isolated tree. Declarative completed a full NMake build after some large targets were built with scoped jom commands; a later cache change to `nmake` was checked with a smaller target, not another complete build.

For a dated private recovery record, retain the Craft package database, local Craft/blueprint patches, Qt source archive, installed-file hashes, source and tool commits, configuration summaries, build logs, and test results together. The recovery tool captures many of these inputs, but it records hashes of installed dependencies rather than bundling an installable Craft prefix. Before claiming a clean-machine reproduction, restore or bootstrap the dependency set in a disposable prefix, rebuild the source there, install it, and check the PDF/PS loaders and normal desktop behavior. Keep the original generated Qt build trees until the separately reviewed cleanup gates pass.

Do not publish `C:\CraftRoot\etc`, CMake caches, build logs, `%LOCALAPPDATA%\okular`, `%LOCALAPPDATA%\okularrc`, or local PDF files. These can contain usernames, document paths, session history, or machine details. The repository's public URL identifies its hosting account; source documentation cannot make that account anonymous.
