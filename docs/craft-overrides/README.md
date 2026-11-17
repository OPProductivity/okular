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
