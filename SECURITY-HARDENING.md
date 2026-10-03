# Security hardening: October 3, 2026

This change addresses the 17 findings (3 medium, 14 low) from the Codex Security
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

## Verification

Built with Craft, Windows x64, MSVC 2022, Qt 6.11.1 and KF 6.27. New regressions
cover malformed PDF actions/choices/signature events, FictionBook depth and
column spans, EPUB empty media/SVG nodes, XPS missing parts and malformed image
brushes, DVI spans/offsets/exact page-number writes, bounded copies, helper
runtime/output/exit status, and Windows helper descendant termination.

Focused existing JavaScript, EPUB/comic rendering, and fork tab/session tests also
pass. Across seven test executables, the selected runs report 58 passing QtTest
cases (including setup/cleanup), no failures, and two skipped archive cases. No print-dialog tests or real user session are used.

Two Okular archive integration tests skip when the runtime MIME database lacks
`application/vnd.kde.okular-archive`. This Craft setup has that limitation; the
bounded streaming helper itself is tested. Linux/macOS process-group handling
is implemented but has not been run on those systems. This round does not claim
ASan coverage, a complete upstream test-suite pass, a formal performance
benchmark, or a fresh clean cloud scan.
