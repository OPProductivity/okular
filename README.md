# Okular for Windows: Single-Window Tabs, Session Restore, and Window Placement

> **Unofficial Windows-focused source fork.** This branch builds on KDE Okular and is maintained independently by OPProductivity. The KDE downloads linked below are upstream releases and do not include this fork's changes.

## Changes in this fork

- Regular Windows file opens, including several files opened at once from File Explorer, join one tabbed Okular window.
- The tab bar remains visible with one document. An adjacent `+` button opens another document, and crowded tabs retain usable scroll controls.
- Open local documents and the active tab can be restored at the next launch; this can be turned off in Okular's settings.
- A window that has moved beyond the screen is brought back into view when another file is opened in it. An intentionally placed, fully visible window keeps its position.

These are changes to the application source. A build on another computer uses that computer's own document paths and settings. No personal documents, local build directories, or Windows installation files are part of this fork.

### Building on Windows

Follow KDE's [Craft setup guide](https://develop.kde.org/docs/getting-started/building/craft/) to install the compiler and dependencies. In a Craft PowerShell environment, change to your clone of this repository and build that checkout:

```powershell
craft --ignoreInstalled --options "kde/applications/okular.srcDir=$((Get-Location).Path)" kde/applications/okular
```

Craft's installation directory is chosen on the builder's machine; the source code does not depend on the maintainer's Windows paths. Check the executable produced by your build before changing file associations or shortcuts.

## Upstream Okular

Okular can view and annotate documents of various formats, including PDF, Postscript, Comic Book, and various image formats.
It supports native PDF annotations.

### Downloads

For download and installation instructions, see https://okular.kde.org/download.php

### User manual

https://docs.kde.org/?application=okular&branch=stable5

### Bugs

https://bugs.kde.org/buglist.cgi?product=okular

Please report bugs on Bugzilla (https://bugs.kde.org/enter_bug.cgi?product=okular), and not on our GitLab instance (https://invent.kde.org).

### Mailing list

https://mail.kde.org/mailman/listinfo/okular-devel

### Source code

https://invent.kde.org/graphics/okular.git

The Okular repository contains the source code for:
 * the `okular` desktop application (the “shell”),
 * the `okularpart` KParts plugin,
 * the `okularkirigami` mobile application,
 * several `okularGenerator_xyz` plugins, which provide backends for different document types.

### Apidox

https://api.kde.org/okular/html/index.html

## Contributing

Okular uses the merge request workflow.
Merge requests are required to run pre-commit CI jobs; please don’t push to the master branch directly.
See https://community.kde.org/Infrastructure/GitLab for an introduction.

### Build instructions

Okular can be built like many other applications developed by KDE.
See https://community.kde.org/Get_Involved/development for an introduction.

If your build environment is set up correctly, you can also build Okular using CMake:

```bash
git clone https://invent.kde.org/graphics/okular.git
cd okular
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir ..
make
make install
```

Okular also builds tests in the build tree. To run them, you have to run `make install` first.

If you install Okular in a different path than your system install directory it is possible that you need to run

```bash
source prefix.sh
```

so that the correct Okular instance and libraries are picked up.
Afterwards one can run `okular` inside the shell instance.
The source command is also required to run the tests manually.

As stated above, Okular has various build targets.
Two of them are executables.
You can choose which executable to build by passing a flag to CMake:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir -DOKULAR_UI=desktop ..
```
Available options are `desktop`, `mobile`, and `both`.

### clang-format

The Okular project uses clang-format to enforce source code formatting.
See [README.clang_format](https://invent.kde.org/graphics/okular/-/blob/master/README.clang-format) for more information.
