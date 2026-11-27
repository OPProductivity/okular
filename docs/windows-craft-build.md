# Build this Okular fork on Windows with Craft

This guide builds the source in this repository. KDE's official Okular download does not contain these changes. It describes a source build, not a redistributable installer or a byte-for-byte reproduction of a previous binary. A fresh Craft installation on a second PC still needs to be tested; Craft and its dependency cache change over time.

## Automated public build

Install [Git for Windows](https://git-scm.com/install/windows), [Python 3.13 or newer](https://www.python.org/downloads/windows/), and the Visual Studio 2022 x64 C++ workload and SDK listed below. Then run these commands in PowerShell:

```powershell
git clone https://github.com/OPProductivity/okular.git C:\src\okular
Set-Location C:\src\okular
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
python -B scripts/windows-craft-build.py --work-dir C:\OkBuild --vcvars $vcvars
```

Use the `vcvars64.bat` for your installed Visual Studio edition. The first
run needs an empty workspace with enough space for downloads and build
files. The script creates an isolated Python environment for the TLS
certificate package and initializes KDE Craft with MSVC 2022 x64 and Ninja
disabled. Craft also provides CMake, CTest and jom. It obtains the required native packages from KDE's binary
cache or runs the corresponding public recipes with Microsoft NMake. The script compiles this
checkout into `C:\OkBuild\prefix` with NMake Makefiles. It uses jom for the
app build and retries with Microsoft NMake if jom rejects a generated rule.
It runs CTest and the Windows shell checks,
and opens a test PDF offscreen. Step logs are in the workspace. Rerun with
the same workspace after a transient failure. Add `--dependencies-only` to
stop after provisioning and checking the native dependencies.

If a download from an MSYS2 mirror fails, the script can retry with the
primary MSYS2 server first in the workspace's mirror list. The existing
fallback mirrors and package signature checks still apply.

If you downloaded a ZIP of the source, extract it and run the Python command
from that directory. Craft still uses Git to obtain its own recipes.

The [package reconciliation](craft-overrides/PublicWindowsPackages.md)
explains where the 54 unmatched entries from the recorded reference build
come from on the public route. The [private replay](windows-craft-clean-prefix.md)
is for installations that need the saved dependency files from that earlier build.

## Manual setup

## 1. Install the tools

Use 64-bit Windows 10 or 11, Git for Windows, Python and CMake with CTest. The
verified replay used Python 3.14.10 and CMake 4.4.4. The recovery helper requires
Python 3.13 or newer. Select the Python supported by your pinned Craft checkout
when bootstrapping Craft itself.

Install Visual Studio 2022 Community or Build Tools with **Desktop development
with C++**. In the installer, verify the x64/x86 MSVC v143 tools and a Windows
SDK under **Individual components**. The verified build selected toolset
14.44.35207 and SDK 10.0.26100.0. The toolset and SDK supply `cl.exe`,
`link.exe`, `nmake.exe`, `rc.exe`, and `mt.exe`. If building Qt Speech from
source with the Windows SAPI backend, select the matching **C++ ATL for v143
build tools (x86 & x64)** component as well. The SDK alone does not supply
`atlbase.h`. [Visual Studio Build Tools component directory](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=visualstudio)

Enable Windows Developer Mode and follow the
[KDE Windows Craft setup instructions](https://develop.kde.org/docs/getting-started/building/craft/craft-windows/)
for Python certificates, PowerShell execution policy, and Craft bootstrap.
Choose the x64 MSVC 2022 ABI and a short local Craft path such as `C:\CraftRoot`.

Use the **Developer PowerShell for VS 2022** shortcut for the remaining commands. It makes MSVC and the Windows SDK available. If Craft was installed elsewhere, replace `C:\CraftRoot` in every command below with that root. If a command reports a missing tool, check it before proceeding:

```powershell
git --version
python --version
cmake --version
ctest --version
Get-Command cl.exe, link.exe, nmake.exe, rc.exe, mt.exe -ErrorAction Stop
$env:VSCMD_ARG_TGT_ARCH
$env:VCToolsVersion
$env:WindowsSDKVersion
```

Craft's installer and options may change; the KDE guide is authoritative for bootstrap. Do not copy another person's `CraftSettings.ini`, local configuration, or application data into the new installation.

## 2. Check out the fork

Choose a source directory outside `C:\CraftRoot` (the example uses a short path). To rebuild a particular known source revision, check out its commit; otherwise stay on `master` for the newest code.

```powershell
New-Item -ItemType Directory -Force C:\src | Out-Null
git clone https://github.com/OPProductivity/okular.git C:\src\okular
Set-Location C:\src\okular
git rev-parse HEAD
# Optional: git switch --detach 53cbc1cb106aabe325c40f13a656331f542a8071
```

The example commit is the source pin for the verified clean-prefix replay
and includes the fixture-test isolation fixes. Record the commit you build.
The [replay profile](craft-overrides/CleanPrefix.reference.json) and
[Craft overrides](craft-overrides/README.md) record the reference Craft and
blueprint revisions, local patches and dependency versions.

## 3. Prepare Craft and build the dependencies

This fork was last rebuilt with MSVC 2022 x64, NMake Makefiles, and `jom`. In `C:\CraftRoot\etc\CraftSettings.ini`, set these entries under the existing `[Compile]` section (keep the other settings made by Craft):

```ini
[Compile]
UseNinja = False
MakeProgram = jom
```

Start a new Developer PowerShell after editing the file. Enter Craft's environment, then install its build tool and the dependencies for the Okular blueprint:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --use-cache dev-utils/jom
if ($LASTEXITCODE -ne 0) { throw 'Craft could not install jom' }
craft --use-cache --install-deps kde/applications/okular
if ($LASTEXITCODE -ne 0) { throw 'Craft could not install the Okular dependencies' }
craft --use-cache libs/qt/qtsvg libs/qt/qtdeclarative libs/qt/qtspeech
if ($LASTEXITCODE -ne 0) { throw 'Craft could not install the required or feature-specific Qt modules' }
```

The dependency step can use KDE's cache and build packages from their recipes when it cannot use a cached package. The explicit Qt requests cover SVG, which this checkout requires, PDF JavaScript, and text-to-speech, including cases where the Okular blueprint leaves those modules implicit. Use the Qt packages from the same Craft installation and ABI. The next section's CMake source argument selects this checkout, independent of any Okular executable offered by the Craft cache. If Craft reports a Python blueprint problem, check its currently supported Python versions first. The reference environment had local Craft fixes, described below. Apply them only if a fresh Craft has the same problem.

### Confirm feature dependencies before compiling

Use the CMake configure summary to confirm both required packages and optional
features. A successful configure can still omit a document backend. The
reference desktop build includes:

| Feature | Check in the Craft prefix or CMake summary |
| --- | --- |
| Build-time Qt and KDE tools | Qt development modules and `moc.exe`, `rcc.exe`, `uic.exe`, plus KF6 Config’s `kconfig_compiler_kf6.exe` and KF6 DocTools’ `meinproc6.exe`. Runtime DLLs alone cannot supply these. |
| PDF rendering and PDF JavaScript | Poppler with its Qt 6 development files and `poppler-qt6.dll`, plus Qt Declarative’s Qml module. |
| PostScript and EPS | libspectre and the matching Ghostscript DLL. |
| Speech, media, image and UI support | Matching Qt Text-to-Speech and its SAPI/WinRT plugins, Qt Multimedia Widgets, Qt SVG, Breeze/KF6 color schemes, and the Qt platform plugins. |
| Additional document formats | FreeType, TIFF, DjVuLibre, ebook-tools/EPub, QMobipocket6, and Discount via prefix `pkg-config.exe`. |

The verified Windows build printed notices for absent `appstreamcli`, Linux
allocator probes and optional Windows icon sizes. Missing `appstreamcli`
skips the AppStream test. Check the PDF and PostScript generators separately
if either dependency is absent from the summary.

[KDE's Windows Craft guide](https://develop.kde.org/docs/getting-started/building/craft/craft-windows/) says Craft normally downloads precompiled cached dependency packages, with an option to compile packages locally. Thus a fresh-PC Craft setup that builds this Okular checkout is not the same claim as compiling every Qt, KDE Frameworks, PDF and Ghostscript dependency from source. The latter has not been demonstrated for this fork; some retained packages or historical cache versions may need new recipe work if their downloads are unavailable. The reviewed patches below preserve known local edits, but they cannot replace unavailable package or source bytes.

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

The 21 November 2026 installed package database recorded QtBase `6.11.3-1`, most Qt 6 modules `6.11.3` (Qt Image Formats and Qt Translations `6.11.1`), KDE Frameworks mostly `6.27.0`, Poppler `26.11.0`, libspectre `0.2.12`, and Ghostscript `10.08.0`. The [sanitized inventory of all 202 package names and versions](craft-overrides/CraftPackages.reference.json) is in Git. These are observed reference versions, not a lockfile or a guarantee that a later Craft cache still serves the same packages. Installed-file hashes and machine-specific revision metadata remain private.

The two [sanitized reference settings files](craft-overrides/README.md) preserve the exact non-sensitive values that influenced this profile. They include the x64 MSVC ABI, RelWithDebInfo/NMake/jom settings, the ignored Ninja/Craft Python packages, and QtBase, Declarative, Poppler and other blueprint options. They are reference excerpts, not complete settings files or universal defaults; inspect the available options in the pinned Craft/blueprint versions before merging them. The reference machine had automatic daily blueprint updates enabled, so recheck the actual checkout revisions before a build. Do not publish or copy the entire local settings files, which may contain paths or private values.

For the closest rebuild on another PC, bootstrap Craft from KDE's current Windows instructions, start from the public Craft parent commit `1ef5a8fd0c8a53c3afd062a4d229a92e161b93f9` and public KDE blueprint commit `5e61dd7ef449b7680551cca8a0a98a8fcde4a20b`, then follow [the reviewed patch sequence](craft-overrides/README.md). The reference Craft commit `0e96fbc37` is local and cannot be fetched from KDE. Its only committed code change is preserved as a patch here. Verify the Craft and blueprint code state again before building, since automatic updates can change it. Record the resolved package versions and Qt configure summary, then compile the fork checkout and run the loader checks below. This sequence has **not** been completed on a second PC. A new Craft environment may require adapting patches or package versions.

## 4. Compile the checkout with NMake and run its tests

Run these commands in the same Developer PowerShell with Craft's environment active. Use a new build directory on the first run. Reuse it for later incremental builds of the same checkout and generator.

```powershell
$craftRoot = 'C:\CraftRoot'
$source = (Resolve-Path C:\src\okular).Path
$build = 'C:\src\okular-build'
$jom = Join-Path $craftRoot 'dev-utils\bin\jom.exe'
if (!(Test-Path -LiteralPath $jom -PathType Leaf)) { throw "Install jom through Craft first" }
$env:PATH = "$craftRoot\bin;$craftRoot\dev-utils\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$craftRoot\plugins"
cmake -S $source -B $build -G 'NMake Makefiles' `
  "-DCMAKE_MAKE_PROGRAM=$jom" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  "-DCMAKE_INSTALL_PREFIX=$craftRoot" "-DCMAKE_PREFIX_PATH=$craftRoot" `
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF `
  -DCMAKE_REQUIRE_FIND_PACKAGE_Poppler=ON `
  -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON -DBUILD_WITH_QT6=ON
if ($LASTEXITCODE -ne 0) { throw 'Okular configuration failed' }
cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'Okular build failed' }
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir $build --output-on-failure
$testResult = $LASTEXITCODE
Remove-Item Env:QT_QPA_PLATFORM
if ($testResult -ne 0) { throw 'Okular tests failed' }
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

The November 2026 cold Qt module probes used retained Qt 6.11.3 source trees and the **already installed** Craft prefix. The first fresh Okular probe also linked against that prefix. Matching `FEATURE_` cache entries checks feature choices. Compiler options, plugins and runtime behavior require their own checks. The later empty-tree QtBase and Declarative runs passed configure and full compilation with NMake Makefiles. QtBase used jom and Declarative used Microsoft NMake. Both probes ended after compilation.

For an app build using a verified private export of the exact installed
dependency files, use the [clean Craft prefix recipe](windows-craft-clean-prefix.md).

For a dated private recovery record, retain the Craft package database, local Craft/blueprint patches, Qt source archive, installed-file hashes, source and tool commits, configuration summaries, build logs, and test results together. The recovery tool captures many of these inputs, but it records hashes of installed dependencies rather than bundling an installable Craft prefix. Before claiming a clean-machine reproduction, restore or bootstrap the dependency set in a disposable prefix, rebuild the source there, install it, and check the PDF/PS loaders and normal desktop behavior. Keep the original generated Qt build trees until the separately reviewed cleanup gates pass.

Do not publish `C:\CraftRoot\etc`, CMake caches, build logs, `%LOCALAPPDATA%\okular`, `%LOCALAPPDATA%\okularrc`, or local PDF files. These can contain usernames, document paths, session history, or machine details. The repository's public URL identifies its hosting account; source documentation cannot make that account anonymous.
