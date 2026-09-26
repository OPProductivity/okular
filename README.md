# Okular for Windows: Single-Window Tabs, Session Restore, and Window Placement

> **Unofficial Windows-focused source fork.** This branch builds on KDE Okular and is maintained independently by OPProductivity. The KDE downloads linked below are upstream releases and do not include this fork's changes.

## Changes in this fork

- Regular Windows file opens, including several files opened at once from File Explorer, join one tabbed Okular window. A second launch from a shortcut raises the existing window.
- The tab bar remains visible with one document. An adjacent `+` button opens another document. On a crowded tab bar, `<` and `>` select neighboring documents, while `o` centers the active tab when space permits.
- Open local documents and the active tab can be restored at the next launch; this can be turned off in Okular's settings. If the launch names another PDF, it opens as a tab to the right of the restored tabs.
- The welcome screen lists up to 50 documents opened within the last 72 hours, ten per numbered page. It can reopen all listed documents across pages in one step, or reopen a Ctrl/Shift selected subset from the current page.
- Revealing selected files in their containing folders groups files by folder and limits each action to five folders.
- Regular Windows launches place the normal Okular window at the top-left of the primary screen with screen-sized geometry, even after a prior window was moved or maximized. This also applies when another file joins an existing window. Explicit full-screen mode remains available.

These are changes to the application source. A build on another computer uses that computer's own document paths and settings. No personal documents, local build directories, or Windows installation files are part of this fork.

### Build and use this fork on Windows

Follow KDE's [Craft setup guide](https://develop.kde.org/docs/getting-started/building/craft/) to install Craft and its Windows compiler and dependencies. In a Craft PowerShell environment, clone **this fork** and build that checkout:

```powershell
git clone https://github.com/OPProductivity/okular.git
Set-Location .\okular
craft --ignoreInstalled --options "kde/applications/okular.srcDir=$((Get-Location).Path)" kde/applications/okular
```

Run `bin\okular.exe` in your Craft installation (for example, `C:\CraftRoot\bin\okular.exe` when Craft is installed at `C:\CraftRoot`). Regular Windows file launches open in tabs even with a fresh Okular configuration. In **Settings > Configure Okular > General**, enable **Open new files in tabs** if you also want files dropped into a document to open in tabs. Enable **Restore previously open documents on launch** if you want session restore; this setting is on by default. On the welcome screen, use the numbered pages and arrows to browse recent documents. **Open All** opens every listed recent document across all pages in the same window. To open only some, Ctrl-click individual entries or Shift-click a range on one page, then choose **Open Selected** or right-click the selection. The selection's context menu can also copy paths (one per line), reveal files in up to five containing folders per action, or forget those entries without deleting the files. Already open tabs are reused; missing local files are skipped. Older recent entries without a recorded open time leave the list when first opening this build; saved tabs still restore. These are per-user settings and are not copied from the maintainer's computer.

To check the result, open two local PDFs in the built Okular and confirm that they appear as tabs in one window. Close Okular, launch it again without a filename, and confirm that the same two files return. To use this build from File Explorer, right-click a PDF, choose **Open with > Choose another app > Choose an app on your PC**, select the built `okular.exe`, and choose **Always** for PDF files ([Windows Open with guidance](https://support.microsoft.com/en-us/windows/experience/storage-filemanagement/common-file-name-extensions-in-windows)). Repeat the two-file check from Explorer. Existing shortcuts or pinned taskbar entries may still point to another Okular installation, so check their targets too.

Craft's installation directory is chosen on your machine; this source does not depend on the maintainer's Windows paths. This fork currently provides source code and build instructions, not a prebuilt Windows installer. A clean clone of this public fork compiled fully with the maintainer's existing Craft dependencies (Windows, MSVC 2022 x64), and focused tab and session tests passed. A fresh installation of Craft on another Windows machine has not yet been tested.

## License

This fork retains Okular's file-level copyright and SPDX license notices. The repository contains files under multiple licenses; consult each file's `SPDX-License-Identifier` where present and the corresponding texts in [LICENSES](LICENSES/) for its terms. The Windows changes are distributed as part of those source files under their respective licenses.

## Upstream Okular (reference)

The following is upstream project information. Its KDE download and clone links produce upstream Okular, without this fork's Windows changes. Use the Windows instructions above to build this fork.

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
