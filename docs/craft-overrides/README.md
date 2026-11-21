# PDF dependency recipe overrides (17 November 2026)

These files preserve the local Craft recipe changes used to build and test
Poppler 26.11.0 and libspectre 0.2.12 on Windows with MSVC, NMake Makefiles,
and `jom`. On 17 November 2026, both packages were installed through Craft
after isolated checks. Okular was then rebuilt against the installed headers
and libraries, and its 32 registered tests passed. An installed-library
offscreen shell test passed; a foreground visual check was not performed.

The recipe diff was made against KDE blueprint commit
`5e61dd7ef449b7680551cca8a0a98a8fcde4a20b`. It changes only
`qt-libs/poppler/poppler.py` and `libs/libspectre/libspectre.py`.
It does not include local Qt recipe changes. Apply it to a matching blueprint
checkout only after inspecting any intervening upstream changes:

```powershell
$blueprints = 'C:\CraftRoot\etc\blueprints\locations\craft-blueprints-kde'
$overrides = 'C:\src\okular\docs\craft-overrides'
git -C $blueprints apply --check "$overrides\pdf-dependencies-2026-11-17.patch"
git -C $blueprints apply "$overrides\pdf-dependencies-2026-11-17.patch"
Copy-Item "$overrides\libspectre-0.2.12-windows.diff" `
  "$blueprints\libs\libspectre\libspectre-0.2.12-windows.diff"
```

The libspectre patch ports its older Windows CMake build to 0.2.12, including
the Ghostscript DLL link and MSVC C compatibility. The recipes specify source
archive SHA-256 digests and retain Poppler's Qt 6, C++, NSS, GPGME, Brotli,
and Harfbuzz support. Keep `UseNinja = False` and `MakeProgram = jom` in the
Craft `[Compile]` settings before building these packages. Back up any installed
package before merging an update; Craft removes the old package's recorded files
as part of `qmerge`.

The isolated checks rendered five repository PDF fixtures through Poppler
26.11.0. Page counts, dimensions, and RGBA pixel hashes matched the previous
26.03.0 build. They also exercised Letter PostScript and offset EPS through
libspectre 0.2.12. The Okular PostScript safety test and four PDF related tests
passed with the staged DLLs first on the test process's `PATH`. The installed
Poppler and libspectre package images were merged with `craft --qmerge` and
`craft --post-qmerge` after closing Okular. The installed Poppler DLL hashes
matched the staged Craft image, and the installed Okular PDF plugin loaded.

## Local QtBase 6.11.3 patch inputs

The two `CVE-2026-116337-qtbase-6.11-000*.diff` files are exact copies of the
local Craft blueprint's Qt 6.11.3 patch files as inspected on 18 November 2026.
`qtbase-security-2026-11-16.patch` preserves the corresponding local recipe
change against KDE blueprint commit `5e61dd7ef449b7680551cca8a0a98a8fcde4a20b`.
Review the patch headers and current upstream Qt/KDE state before applying them
to another version; do not apply them blindly or duplicate an upstream fix.
The unpacked local QtBase source contains both changes. This preservation does
not establish that the installed Qt binaries or the Qt network regression
tests were rebuilt and qualified with these patches.

## Local Craft core changes

The reference Craft checkout is `0e96fbc37aa76adef5b5e60d10ccf9cd477a2667`.
That local commit adds `urllib.parse` and `urllib.request` imports to
`bin/Utils/GetFiles.py`; `craft-getfiles-2026-11-18.patch` preserves the exact
change relative to its parent `1ef5a8fd0c8a53c3afd062a4d229a92e161b93f9`.
It is already present if you use commit `0e96fbc37` and should not be applied
twice. A newer upstream Craft may also contain the fix.

The reference checkout additionally has uncommitted, machine-local changes to
`bin/BuildSystem/PipBuildSystem.py` and `blueprints/libs/python/python.py`.
`craft-python-ignore-2026-11-21.patch` is the exact two-file diff against
Craft commit `0e96fbc37`. It makes pip use the system/venv Python when the
Craft Python package is ignored and changes the local Python blueprint target
from `3.12.13` to `3.14.6`. The current system Python version can differ from
that blueprint target. This patch is reference evidence, not a universal Craft
update; inspect the present upstream code and use it only if the same problem
is reproduced. To check it against a clean checkout at the recorded commit:

```powershell
$craft = 'C:\CraftRoot\craft'
$overrides = 'C:\src\okular\docs\craft-overrides'
git -C $craft apply --check "$overrides\craft-python-ignore-2026-11-21.patch"
# Apply only after reviewing the diff and confirming the workaround is needed.
git -C $craft apply "$overrides\craft-python-ignore-2026-11-21.patch"
```

Both patches contain code only; no personal settings, build logs, package
database or document paths are published here. The original Craft and blueprint
checkouts and their private recovery snapshot remain separate from this repo.

### Reconstruct the reference code state on a new Craft installation

The two base commits below are on KDE's public Craft and blueprint remotes. Use
fresh, clean checkouts: do not switch an existing checkout with local changes.
Review each patch and run `git apply --check` before `git apply`. The commands
below reconstruct the reference source/recipe edits, not the installed binary
packages or a complete Craft installation:

```powershell
$craft = 'C:\CraftRoot\craft'
$blueprints = 'C:\CraftRoot\etc\blueprints\locations\craft-blueprints-kde'
$overrides = 'C:\src\okular\docs\craft-overrides'
git -C $craft fetch origin
git -C $craft switch --detach 1ef5a8fd0c8a53c3afd062a4d229a92e161b93f9
git -C $craft apply --check "$overrides\craft-getfiles-2026-11-18.patch"
git -C $craft apply "$overrides\craft-getfiles-2026-11-18.patch"
# Only if Craft Python is deliberately ignored and the pip issue is reproduced:
git -C $craft apply --check "$overrides\craft-python-ignore-2026-11-21.patch"
git -C $craft apply "$overrides\craft-python-ignore-2026-11-21.patch"

git -C $blueprints fetch origin
git -C $blueprints switch --detach 5e61dd7ef449b7680551cca8a0a98a8fcde4a20b
git -C $blueprints apply --check "$overrides\pdf-dependencies-2026-11-17.patch"
git -C $blueprints apply "$overrides\pdf-dependencies-2026-11-17.patch"
git -C $blueprints apply --check "$overrides\qtbase-security-2026-11-16.patch"
git -C $blueprints apply "$overrides\qtbase-security-2026-11-16.patch"
Copy-Item "$overrides\libspectre-0.2.12-windows.diff" `
  "$blueprints\libs\libspectre\libspectre-0.2.12-windows.diff"
Copy-Item "$overrides\CVE-2026-116337-qtbase-6.11-0001.diff" `
  "$blueprints\libs\qt6\qtbase\CVE-2026-116337-qtbase-6.11-0001.diff"
Copy-Item "$overrides\CVE-2026-116337-qtbase-6.11-0002.diff" `
  "$blueprints\libs\qt6\qtbase\CVE-2026-116337-qtbase-6.11-0002.diff"
```

The reconstructed Craft files and blueprint recipes were compared with the
reference local edits on 21 November 2026 and matched after line-ending
normalization. The three copied upstream-source patch files matched byte for
byte. This is a code-state reconstruction check, not a clean-machine Craft
bootstrap, dependency download, build, installation or runtime test.
