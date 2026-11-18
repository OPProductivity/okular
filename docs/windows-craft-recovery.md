# Preserve Craft reconstruction inputs before retiring Qt build output

`scripts/windows-craft-recovery.py` is a local capture, integrity-check and
reconstruction-probe tool. It has **no delete, recycle, install or app-launch
operation**. It writes machine-private evidence outside the source checkout;
do not commit that evidence or its configuration files to this repository.

## What can be considered separately

Only these fixed module directories are eligible for the generated-build plan:
`build/libs/qt6/{qtbase,qtdeclarative,qt5compat,qtshadertools,qttools,qtlanguageserver,qtsvg,qtmultimedia,qtspeech}/work/build`.
The tool requires a CMake cache, refuses a source CMakeLists.txt at a candidate
root and rejects a reparse/escaped root. Do not select `build/libs` or `work` as
a whole: they also contain patched sources, staging images and debug symbols.

Keep the installed prefix, current Okular incremental build, all source trees,
package/staging images, the Qt base `build-nmake-*` diagnostic tree, current
compiler/SDK/jom/CMake, package database and local patches. Ordinary Okular
changes use those installed dependencies. A future Qt patch needs a distinct,
potentially expensive dependency rebuild.

## Capture and verify

From this source checkout, choose an **absent** directory outside Craft and the
checkout. The following paths are examples; do not reuse an old snapshot name:

```powershell
$snapshot = 'C:\recovery\craft-20261118'
python scripts/windows-craft-recovery.py capture `
  --craft-root C:\CraftRoot --source-root (Get-Location).Path --output $snapshot
python scripts/windows-craft-recovery.py verify --snapshot $snapshot
python scripts/windows-craft-recovery.py plan --snapshot $snapshot `
  --output "$snapshot\recycle-plan.md"
```

Capture includes Git bundles for Okular, Craft and the blueprint checkout;
working-tree patches including staged changes; selected untracked patch/source
files; Craft settings and a consistent SQLite package-database backup. It saves
the actual patched Qt sources plus build/configuration metadata in a compressed
archive, records candidate file identities/sizes, and hashes retained source and
installed-prefix inputs. Untracked files outside the supported patch/source
extensions are reported as excluded rather than silently treated as protected.
The current source and runtime remain at their original paths.

`verify` checks the saved artifact hashes, each retained Qt source member
against its recorded SHA-256, protected current-file hashes and Git bundles,
and refuses installed CMake/pkg-config references to a selected build
directory. Future captures prune generated build trees and Craft/blueprint Git
bookkeeping from the installed-runtime hash walk; the original 18 November
snapshot predates that correction and may need a fresh snapshot after Git
commits. It does not turn a complete capture into cleanup authorization.
`plan` also compares the current candidate path/file-ID/size/timestamp inventory
with the captured inventory before writing a plan. A plan is still read-only.

## Prove the retained development route

Use a new build directory outside both Craft and the source checkout. The probe
collects the VS 2022 x64 environment without logging it, configures NMake/jom,
and optionally compiles only the app target. It never installs or launches it:

```powershell
python scripts/windows-craft-recovery.py probe --snapshot $snapshot `
  --build-dir C:\recovery\okular-clean-probe --compile
```

The probe leaves existing Qt build directories present. Success establishes a
fresh app build against the retained installed dependencies; it is not an
absence test for every historical generated file.

For an add-on Qt dependency, a separate cold probe reconstructs a standard
no-tests/no-examples build from the retained module source. It explicitly
overrides `QT_SOURCE_TREE` with the retained Qt base source path, avoiding stale
`Z:` source mappings in installed build-internals metadata:

```powershell
python scripts/windows-craft-recovery.py probe-qt --snapshot $snapshot `
  --module qtsvg --build-dir C:\recovery\qtsvg-clean-probe --compile
```

This is a functional reconstruction profile, not exact historical feature or
binary-hash parity. Review the preserved CMake cache and configure summary when
a different module or feature set is required. Qt base itself is deliberately
excluded from this generic add-on probe. Do not restore historical Ninja
generator/cache settings as current NMake instructions.

## Later explicitly authorized Recycle Bin execution

The generated plan names exact directories; it is not an executable cleanup
script. Immediately before any later authorized move, reverify recovery and
protected files, current build/process/path consumers, exact tree membership and
reparse boundaries. Retain all evidence outside the selected targets. Use a
native Windows Recycle Bin operation with no permanent-delete fallback; verify
each matching original-path metadata record and payload before continuing.
Stop on a failed move **or** failed post-move verification. Do not empty the bin.
Same-volume recycling does not immediately free the payload bytes.

Keep the current Okular build and sources through the next five ordinary
application improvement rounds, along with installed dependencies, staging
debug symbols and the recovery snapshot. This preserves incremental app work;
it does not promise that arbitrary Qt dependency changes will be instantaneous.

## Checks for this helper

```powershell
python scripts/tests/test_windows_craft_recovery.py
```

The tests exercise candidate boundaries, source-tree rejection, evidence
overwrite refusal, installed-path references, archive path escape and corruption,
source-member hash mismatch, protected-tree pruning, runtime drift and incomplete
snapshots. They are not GUI or Qt runtime tests.
