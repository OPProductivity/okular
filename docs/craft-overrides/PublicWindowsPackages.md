# Public Windows package sources

The public build uses one Craft package graph for Windows x64 MSVC 2022.
The graph selects dependency versions for a functional build of this fork.
The [private replay](../windows-craft-clean-prefix.md) preserves the exact
files from the older reference installation.

On 27 November 2026, the 26.05 and 26.10 MSVC cache manifests contained a
same-version candidate for 148 of the 202 records in
[`CraftPackages.reference.json`](CraftPackages.reference.json). The 54 other
records are covered below. Many are aliases, host tools or provider recipes
whose records do not correspond to an archive in those two indexes.

| Count | Reference paths | Provisioning in the public build |
| --- | --- | --- |
| 2 | `craft/craft-core`, `craft/craft-blueprints-kde` | Public KDE Git checkouts. The script pins both revisions before provisioning Okular. |
| 9 | `dev-utils/7zip`, `dev-utils/cmake`, `dev-utils/git`, `dev-utils/kshimgen`, `dev-utils/msys`, `dev-utils/msys-base`, `dev-utils/patch`, `dev-utils/sed`, `dev-utils/system-python3` | Craft bootstraps its base tools, shims and tool distributions. Git, Python and the compiler tools must be available to start setup. |
| 1 | `libs/runtime` | The MSVC Craft recipe copies its runtime from the selected Visual Studio toolset. |
| 3 | `virtual/base`, `virtual/craft-blueprints-kde-base`, `virtual/craft-venv` | Craft resolves the virtual packages during bootstrap. They set up the dependency graph and Python environment. |
| 3 | `python-modules/hatch-vcs`, `python-modules/pip-system`, `python-modules/virtualenv` | Craft installs these with its Python recipes when requested by the graph. The launcher installs the bootstrap certificate package in its own venv. |
| 1 | `kde/applications/okular` | Craft resolves its dependencies. The launcher compiles the fork checkout directly with CMake. |
| 4 | `kdesupport/kdewin`, `libs/llvm`, `libs/nettle`, `qt-libs/phonon-vlc` | The public Git or source recipes provide these when selected by the dependency graph. Phonon VLC uses the KDE source archive and VLC dependency for Windows. |
| 9 | `libs/assuan2`, `libs/gcrypt`, `libs/gnupg`, `libs/gpg-error`, `libs/gpgme/gpgme`, `libs/libksba`, `libs/mingw-crt4msvc`, `libs/npth`, `libs/ntbtls` | Craft's MSVC recipes take these from KDE's dedicated MinGW prebuilt feed. On Windows, the `mingw-crt4msvc` recipe takes `libs/runtime` from the same feed. The public 26.10 manifest listed all nine underlying packages. |
| 2 | `libs/libdmtx`, `libs/libogg` | The pinned recipes select 0.7.8 and 1.3.6. Both have candidates in the public 26.10 MSVC cache. |
| 2 | `libs/libspectre`, `qt-libs/poppler` | The launcher applies the fork's tracked PDF recipe changes. Craft obtains the public libspectre 0.2.12 and Poppler 26.11.0 source archives using the recorded digests when those versions are absent in the cache. CMake requires both libraries and the Qt 6 Poppler frontend. |
| 9 | `libs/qt/qtbase`, `libs/qt/qtdeclarative`, `libs/qt/qtimageformats`, `libs/qt/qtmultimedia`, `libs/qt/qtspeech`, `libs/qt/qtsvg`, `libs/qt/qttools`, `libs/qt/qttranslations`, `libs/qt/qtwayland` | These paths select the actual Qt packages for the active Qt ABI. For this build that is Qt 6. Platform-conditional packages are selected by Craft. |
| 9 | `libs/qt6/qt5compat`, `libs/qt6/qtbase`, `libs/qt6/qtdeclarative`, `libs/qt6/qtlanguageserver`, `libs/qt6/qtmultimedia`, `libs/qt6/qtshadertools`, `libs/qt6/qtspeech`, `libs/qt6/qtsvg`, `libs/qt6/qttools` | The pinned blueprint uses the Qt 6.12.0 package family, with candidates in the public 26.10 MSVC cache for all nine modules. The corresponding module recipes provide the source route for cache misses. |

Sources for the lookup include the [26.05 MSVC cache](https://files.kde.org/craft/Qt6/26.05/windows/cl/msvc2022/x86_64/RelWithDebInfo/manifest.json), [26.10 MSVC cache](https://files.kde.org/craft/Qt6/26.10/windows/cl/msvc2022/x86_64/RelWithDebInfo/manifest.json), and [26.10 MinGW prebuilt feed](https://files.kde.org/craft/prebuilt/26.10/windows/mingw/x86_64/RelWithDebInfo/manifest.json). Craft checks the available archives and build configuration when provisioning.
