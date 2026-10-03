# Security hardening: October 3, 2026

The first hardening commit addresses 17 findings (3 medium, 14 low) from the Codex Security
Cloud repository scan of `OPProductivity/okular` at `25c1488c18e7c0ffdce1d496063d54dd213c2b94`.
The original findings were based on static analysis. They have not been replaced
by a fresh cloud scan or independently certified as resolved.

## Changes

| Finding | Repair |
| --- | --- |
| Page-closing PDF Rendition dangling link | Retain ownership of the Poppler link until annotation resolution. Resolve only the matching media action type and tolerate an already resolved handle. |
| DVI export writes through unchecked offsets | Validate every page offset and backward chain, including one-page files. Write exactly four bytes for each page number. |
| Automatic external Sound requests | Block external audio in page lifecycle actions. Explicit external playback requires consent showing its destination; reject unsupported schemes and network shares. |
| Unbounded Ghostscript work | Bound runtime, captured output, and generated file size; terminate owned helper descendants and remove incomplete output on failure. |
| FictionBook ZIP directory cast | Require a file entry and own the resulting input device. |
| EPUB list/string bounds | Check empty anchors and media indices; iterate SVG images using their actual index and advance live DOM lists safely. |
| JavaScript native bounds/null access | Reject negative field indices, tolerate absent layer models, and return undefined for empty choice lists. |
| Automatic Browse URL handlers | Block Browse/Execute actions from page lifecycle events, including chained actions. Clicked links allow standard web, mail, local document, and existing LilyPond source links; reject executable files and network shares. |
| Malformed XPS structures | Check missing archive entries, parent directories, rectangle cardinality, empty image sources, and page dimensions. |
| Comic archive/RAR resource exhaustion | Bound archive traversal, declared expanded sizes, helper runtime/output, and monitored extraction size/count/free space. |
| Empty RAR helper output | Check listing cardinality before removing or accessing entries; respect unsuccessful helper exit codes. |
| Truncated DVI reads | Check complete byte spans before reading, skipping, or writing; validate preamble/postamble/font spans and stop parsing after failure. |
| Signature PDF null script event | Check that an event exists before inspecting its type. Preserve the existing signature policy. |
| FictionBook recursion/table allocations | Preflight XML size, element depth/count, and table dimensions before large allocations; release failed conversion documents. |
| Source-less EPUB video loop | Remove unsupported video nodes so conversion advances. |
| Compressed document expansion | Use bounded streaming copies for compressed wrappers and Okular archive manifests, metadata, and payloads. |
| JavaScript native work exhaustion | Bound popup trees, choice-index arrays, and timers; validate selections and clean up timer ownership. |

## Follow-up scan repairs

The follow-up scan of `85e1c2f8e6381bab732e48c506d3bd076860b466` reported
19 medium findings and one low finding. The following repairs address those
findings without changing the fork's tab/session policy.

| Finding | Repair |
| --- | --- |
| In-process PostScript rendering can run indefinitely | Render each page in an owned, cancellable helper with a wall-time and memory budget; keep generation identities to discard stale results after closing/reopening. |
| DVI export and printing launch unbounded helpers | Apply the helper budget to asynchronous exports and printing; reject failed or oversized results and remove incomplete files. |
| Compressed TXT can create multiple oversized text copies | Limit TXT input to 16 MiB and 100,000 lines, disable undo retention, and reject failed conversion. |
| Nested JavaScript resets/stops the outer watchdog | Only the outermost execution owns the two-second watchdog; nested execution retains interruption state. |
| Automatic external movie loading | Block automatic external Movie/Rendition actions and autoplay; explicit external playback asks for consent. Archive video remains embedded. |
| PS import launches an unowned converter | Own the converter in the document part, apply asynchronous budgets, validate exit/output, and remove its temporary output with the owner. |
| Automatic TeX font generation is unbounded | Bound font lookups/generation by time, count, output, and a private temporary font cache. |
| TIFF dimensions drive unchecked allocation | Validate dimensions and pixel count before loading, rendering, and printing; reject oversized/failed images. |
| PDF Next action chains exhaust recursion | Bound action conversion and dispatch by depth/count, detect cycles, and destroy owned action graphs iteratively. |
| Passive form actions inherit click privileges | Propagate event provenance: only explicit mouse press/release actions receive user-initiated privileges. |
| JavaScript number precision drives excessive work | Reject precision outside 0–100 before formatting. |
| LaTeX document text can access files | Use a bounded math command grammar, private working directory, disabled shell escape, restrictive TeX file policy, and bounded helpers. |
| DVI Ghostscript can read arbitrary paths | Stage only explicitly referenced, validated relative resources into a private directory; grant modern Ghostscript read access only to that directory. |
| XPS resources exceed aggregate budgets | Validate archive members and XML structure before use; share a bounded resource cache and cap decoded image retention. |
| EPUB resources exceed aggregate budgets | Preflight archive expansion, bound chapter/text/image/media retention, and read resources through the shared cache while preserving package-relative paths. |
| PK row repeats write beyond the bitmap | Validate repeat counts and packed-number parsing before either endian-specific write; reject malformed glyphs without a fatal dialog. |
| Automatic external GoTo opens other documents | Require explicit user initiation for external GoTo actions. |
| JavaScript timers defeat per-script limits | Apply callback count/work budgets across timer callbacks, cancel interrupted intervals, and enforce a 10 ms scheduling floor. |
| Automatic document commands (low) | Block passive DocAction requests, including printing and quitting. Explicit user commands remain available. |

## Limits and compatibility

Ordinary uncompressed PDF loading does not gain a document-size cap. The fork's
tab handling, session restore, recent selection, and window placement policy are
unchanged by this hardening.

- Compressed expansion: at most 1 GiB, with a ratio budget of 1,000 times the input
  size and a 64 MiB floor. Okular archive XML/metadata: 16 MiB per member.
- Archive traversal: 10,000 entries and 128 directory levels. Temporary copies
  preserve 16 MiB of available storage where the filesystem reports it.
- External helpers: 120 seconds per operation (5 seconds for RAR version
  detection), 8 MiB each of captured stdout/stderr, and process-tree cleanup.
  RAR extraction is polled every 250 ms; it can transiently exceed its disk
  budget between polls. These limits do not constitute an OS sandbox.
- FictionBook: 64 MiB XML, 128 levels, one million elements, 1,024 table columns
  or column span, and 100,000 table cells.
- JavaScript: 1,000 popup items, 20 submenu levels, 10,000 choice-index entries,
  32 live timers per document / 128 globally, and a 10 ms minimum repeating
  timer interval. These are native-operation limits, not a complete JavaScript
  sandbox.

Unusually large archives/conversions and scripts beyond these limits are rejected.
Automatic links to external applications are intentionally disabled; normal
clicked HTTP/HTTPS/mail links remain available. Embedded sound is unchanged.

Additional follow-up limits:

- Helper process trees: 1 GiB memory; Unix also limits CPU and generated file size.
  Asynchronous output is polled every 50 ms. Windows job attachment occurs after
  startup, so this is resource hardening, not a sandbox guarantee.
- TIFF/EPUB/XPS raster images: at most 32,768 pixels per dimension and 64 million
  pixels. EPUB/XPS archives: 64 MiB per binary member, 16 MiB per text member,
  256 MiB aggregate expanded/retained resources, 10,000 entries and 128 levels.
  Preflight inflation adds work to EPUB/XPS opening; ordinary PDF loading is unaffected.
- EPUB conversion: 16 MiB chapter text, 100,000 blocks, 10,000 spine/TOC sections.
- JavaScript action chains: 64 levels / 1,000 actions. Timer callbacks: at most
  200 callbacks and two seconds of script work in a ten-second window.
- Font lookup/generation: 60 seconds, 512 font names, and 64 MiB of private cache.
- LaTeX: 8 KiB of input, ten seconds per helper, and 64 MiB temporary output.
  Custom TeX programming commands outside the math allowlist remain plain text.
- DVI rendering now requires Ghostscript 9.50 or newer for its explicit
  `--permit-file-read` policy. Absolute/escaping document-supplied PS resources
  are intentionally rejected. PostScript rendering requires the installed
  `okular-spectre-render` helper; process startup and PNG transfer add overhead
  to PostScript pages only.

## Verification

Built with Craft, Windows x64, MSVC 2022, Qt 6.11.1 and KF 6.27. New regressions
cover malformed PDF actions/choices/signature events, FictionBook depth and
column spans, EPUB empty media/SVG nodes, XPS missing parts and malformed image
brushes, DVI spans/offsets/exact page-number writes, bounded copies, helper
runtime/output/exit status, and Windows helper descendant termination.

Focused existing JavaScript, EPUB/comic rendering, and fork tab/session tests also
pass. The initial round reported 58 passing QtTest cases. The follow-up selected
runs across nine executables report 88 passing cases (including setup/cleanup),
no failures, and two skipped archive cases. These include nested watchdog and
aggregate timer regressions, passive form provenance, cyclic/deep action cleanup,
malformed TIFF/PK/TXT inputs, archive/raster limits, asynchronous helper
cancellation, and a nonterminating PostScript helper followed by successful
normal rendering. Ordinary EPUB structure/content and comic image tests pass.
The automated tests use isolated settings and do not trigger real print dialogs.

The complete local installation was updated together, including the executable,
core DLL, part DLL, generator plugins, and PostScript helper. SHA-256 checks
matched all 18 installed binaries to this build. Live smoke checks verified
session restoration, ordinary PDF rendering, tab switching, duplicate-file
focusing, opening a PDF through the Windows default association, scrolling,
and text search. The temporary test tab was closed and the original active
document, page, and zoom were restored.

Two Okular archive integration tests skip when the runtime MIME database lacks
`application/vnd.kde.okular-archive`. This Craft setup has that limitation; the
bounded streaming helper itself is tested. Linux/macOS process-group handling
is implemented but has not been run on those systems. This round does not claim
ASan coverage, a complete upstream test-suite pass, a formal performance
benchmark, or a fresh clean cloud scan.

## Follow-up local regression check

After repairing Windows 11 tab-label painting, the selected security, helper,
JavaScript, form-event, EPUB/comic, and fork-shell runs reported 89 passing
QtTest cases, no failures, and the same two archive MIME skips. Shell coverage
also included the tab controls and right-to-left overflow layout. The installed
app rendered ordinary PDF, EPUB text/images, and PostScript through the new
helper. Both active and inactive tab labels were visually checked in the dark
theme; native Windows painting now supplies their readable text colors while
preserving the selected-tab accent. Original session tabs and reading position
were retained, and temporary test tabs were closed.

## Windows EPUB long paths

The Windows executable now declares `longPathAware` in its embedded manifest.
This lets native EPUB-library file opens use the Windows long-path policy, as
Qt's own file access already does. Normal user privileges remain `asInvoker`;
no system policy is changed. The Windows `LongPathsEnabled` policy must also be
enabled for this opt-in to take effect.

A regression opens a fixture EPUB beyond 300 path characters with an accented
filename. It and five adjacent session/tab tests passed (eight QtTest cases
including setup/cleanup). The locally installed viewer also opened and rendered
all three previously rejected EPUBs at their unchanged original locations,
including a 347-character path, when launched together into an existing session.
The original 16 tabs and active article were restored after closing the test
tabs. MinGW resource embedding is provided but was not built in this MSVC setup.
