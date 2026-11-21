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
Craft's `etc/PristineCraftenv.json` is a regenerated copy of a caller's
environment, which can contain session-specific values. It is not an immutable
installed runtime input. Future captures omit it; verification of an older
capture names it in `excluded_mutable` rather than treating a changed session
cache as a dependency failure. Do not publish its contents.
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

For a Qt dependency, a separate cold probe reconstructs a standard
no-tests/no-examples build from the retained module source. For add-on modules it
overrides `QT_SOURCE_TREE` with the retained Qt base source path, avoiding stale
source mappings in installed build-internals metadata:

```powershell
python scripts/windows-craft-recovery.py probe-qt --snapshot $snapshot `
  --module qtsvg --build-dir C:\recovery\qtsvg-clean-probe --compile
python scripts/windows-craft-recovery.py probe-qt --snapshot $snapshot `
  --module qtbase --build-dir D:\recovery\qtbase-clean-probe --compile
python scripts/windows-craft-recovery.py probe-qt --snapshot $snapshot `
  --module qtspeech --build-dir D:\recovery\qtspeech-clean-probe --compile
```

This is a functional reconstruction profile, not exact historical feature or
binary-hash parity. Review the preserved CMake cache and configure summary when
a different module or feature set is required. Qt Speech uses Microsoft NMake
because jom failed on its generated rule in the reference build; it needs the
MSVC ATL component for the Windows speech plugins. Qt Declarative also uses
Microsoft NMake for the full graph: jom spent over ten minutes loading that
graph without reaching a compile target in the cold probe. The other modules
use jom.
For the retained QtBase 6.11.3 profile, the helper pins Craft's ICU,
pkg-config, linked OpenSSL, system SQLite/FreeType/HarfBuzz and DBus linkage
choices. Those dependencies must remain installed in the Craft prefix. Compare
the generated QtBase `FEATURE_` cache entries with the retained recipe before
using the probe for a newer Qt version.
The probe never installs its output into Craft. Use separate build directories
and check free disk space before starting large modules such as Qt Declarative;
keep several gigabytes of free space beyond the expected build size. The
NMake-generated top-level Makefiles include `.NOTPARALLEL`: CMake ignores
`--parallel` and jom's `/J` cannot override it on that Makefile. Nested jom
invocations may still run targets concurrently. The helper does not promise a
specific worker count; budget time and memory accordingly. Do not restore
historical Ninja generator/cache settings as current NMake instructions.

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
