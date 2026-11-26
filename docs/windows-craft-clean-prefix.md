# Rebuild Okular in a clean Craft dependency prefix

This procedure restores a verified private export of prebuilt Craft
dependencies into an empty prefix, then builds and installs the pinned Okular
source with NMake Makefiles and jom. The
[standard Windows Craft guide](windows-craft-build.md) covers public Craft
bootstrap and package management. The [recovery guide](windows-craft-recovery.md)
covers building dependencies from source.

## Inputs

The [clean-prefix profile](craft-overrides/CleanPrefix.reference.json) pins the
Okular source commit and records the Craft and blueprint commits, MSVC x64
toolset, Windows SDK, CMake, Python and make versions. The referenced inventory
identifies the Qt/KDE and PDF package versions, the 12 archive sizes and
SHA-256 digests for source rebuilds, configure flags for the Qt modules, and
the patches to reproduce locally modified recipes and source. The source
archives are useful when rebuilding a dependency. The clean-prefix app build
uses the separately locked binary payload.

The KDE cache can supply some package versions. Exact package identity
requires checking the archive digest. On 26 November 2026, the 26.05 and
26.10 x64 MSVC indexes listed a same-version candidate for 147 of 202 installed
package records. Another 39 cache-tagged records lacked that match and 16 lacked
a cache label. Preserve the private dependency bundle for replay when a
matching package is unavailable. The bundle records the as-installed file
bytes and their package ownership, including local replacements that differ
from Craft's database hashes.

## Export on the original PC

Use a short, disposable path separate from the installed Craft root, the
repository and synced folders. The examples use a new `C:\OkularReplay` tree
and PowerShell. Run the first line from a checkout containing this guide.
Use Python 3.13 or newer.

```powershell
$recipe = (Resolve-Path .\scripts\windows-craft-prefix.py).Path
$root = 'C:\OkularReplay'
$live = 'C:\CraftRoot'
New-Item -ItemType Directory -Path $root -ErrorAction Stop | Out-Null
python -B $recipe capture --craft-root $live --install-manifest "$live\build\kde\applications\okular\work\build\install_manifest.txt" --output "$root\payload"
python -B $recipe verify --bundle "$root\payload" --kind installed-okular
```

Capture uses Craft's read-only package database and the last Okular CMake
install manifest. It records missing package entries, deduplicates paths, and
omits unrelated unowned files, dependency debug symbols, MSYS, build trees and
user data. The Okular install is archived separately. Record the original
prefix path alongside the bundle. The build needs that string to relocate
imported dependency paths, even on a PC where that prefix was never installed.

Keep `payload.lock.json` beside the two private ZIPs. It contains raw package
revision strings and may contain machine-specific information. Keep it, the
archives, raw logs and build receipts off the public repository. The Okular ZIP
preserves the recorded installed application files. Recovering those binary
bytes still requires compatible dependencies and Windows runtime files. A new
build can pass all functional checks with different app binary hashes.

## Restore on the build PC

Create the short local work directory and make the private bundle available
there, or set `$bundle` to the directory containing the existing private
archive and lock. Leave `$root\prefix` and `$root\build` absent. Allow about
12 GiB free if the work drive will also hold the private ZIPs. The helper
checks the space needed for extraction and requires four additional GiB before
the build. Restore verifies every extracted file by size and SHA-256.

```powershell
$recipe = (Resolve-Path .\scripts\windows-craft-prefix.py).Path
$root = 'C:\OkularReplay'
$bundle = "$root\payload"
python -B $recipe restore --bundle $bundle --destination "$root\prefix"
if ($LASTEXITCODE -ne 0) { throw 'Dependency restore failed' }
```

## Configure, compile, install and test

Create a fresh checkout at the profile's source commit. Use a Visual Studio
2022 x64 `vcvars64.bat` and the matching CMake installation:

```powershell
$commit = '53cbc1cb106aabe325c40f13a656331f542a8071'
git init "$root\src"
git -C "$root\src" fetch --depth=1 https://github.com/OPProductivity/okular.git $commit
git -C "$root\src" switch --detach FETCH_HEAD
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$originalPrefix = 'C:\CraftRoot'
python -B $recipe preflight --prefix "$root\prefix" --cmake $cmake --vcvars $vcvars
if ($LASTEXITCODE -ne 0) { throw 'Build preflight failed' }
python -B $recipe build --source "$root\src" --commit $commit --prefix "$root\prefix" --build-dir "$root\build" --original-prefix $originalPrefix --cmake $cmake --vcvars $vcvars
```

Use the `vcvars64.bat` from your selected Visual Studio 2022 edition. The
preflight checks the x64 compiler, resource and manifest tools, Windows SDK
headers, CTest, and build tools and plugins in the restored prefix. It reports
whether ATL headers are available for a separate Qt Speech source build.
See the [host tools and feature checklist](windows-craft-build.md#1-install-the-tools)
for the Visual Studio components and feature dependencies.

The helper selects the restored jom, records the MSVC/SDK selection, uses
`RelWithDebInfo` and `NMake Makefiles`, and enables Qt 6, shared libraries and
tests. It tells CMake to ignore the original prefix, disables CMake package
registries, checks the generated compiler/linker metadata for references to the
original prefix, and confirms Qt resolves its prefix to the new location.
Some installed package metadata contains an absolute original prefix. The
helper rewrites those references in the extracted CMake, pkg-config and qmake
metadata and records both hashes. The helper writes `receipt.json` and
per-step logs inside the disposable build directory.

Installation targets only the new prefix. The offscreen startup check opens a
repository fixture with Qt's Windows test profile and isolated Okular IPC. It
checks that the new Okular part, Poppler generator, Poppler DLL, Qt Core,
Text-to-Speech and offscreen platform plugin load from the new prefix. CTest
and targeted shell cases exercise rendering, theme and tab handling,
page-counter layout, duplicate opens, recent files and session restoration.
Normal desktop and Explorer integration remain separate checks.

### Preview the rebuilt reader

Launch the executable with the restored plugin paths and a dedicated Okular
session in Qt test mode. The profile is reused when opening more files through
the same command. Use **File > Open**, **Ctrl+O**, or **+** in the preview
window to add more tabs. Windows Explorer continues to use its current file
association.

```powershell
python -B $recipe launch --prefix "$root\prefix" "$root\src\autotests\data\file1.pdf" "$root\src\autotests\data\simple-multipage.pdf"
```

If configure fails, read `configure.log` for the first missing package or
incorrect prefix. If compile fails when launching `moc`, `rcc`, or a KDE helper,
check which Qt DLLs that process resolves. If the app starts with
`--version` but cannot open a PDF, check the installed part and Poppler
generator in `installed-pdf.log`. Use a fresh build directory after changing
the generator or switching prefixes. Restore Qt and KDE development tools,
headers, and import libraries together with the runtime DLLs and plugins.

## Verification on 26 November 2026

The complete build used 49,888 verified dependency files and a fresh checkout
at `976904a0d`. It configured, compiled, installed, and opened a PDF
offscreen. All 1,408 app install paths matched the retained CMake install
manifest, including 14 generators. Thirteen essential dependency DLLs and
plugins matched the installed baseline by SHA-256. The 18 newly compiled
app DLLs and executables had distinct hashes. The separately archived
application files preserve the historical binary bytes.

All 32 CTest tests and eight focused shell cases passed. The first CTest run
revealed that the document and form tests wrote fixture metadata to the normal
DocData directory. Their profile isolation fix is included in the pinned
source above. The fix changes two tests. The application source in the two
revisions is identical. Both test executables were rebuilt and the full
32-test suite passed again with the normal profile matching its pre-test
hashes.

The private payload, restored prefix, fresh app build and evidence occupied
about 8.75 GiB combined on this run. The run verified replay on the build PC.
A second-PC run and a clean bootstrap directly from KDE package archives
remain separate verification steps.
