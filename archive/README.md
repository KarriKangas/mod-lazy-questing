# Previous Lazy Questing project

`lazy-questing-2026-09-05.zip` preserves every working-tree file present before the reset, excluding `.git` and this new archive directory. Each archived file was SHA-256 checked against its original before active files were removed.

Archive SHA-256: `11177E2ACECA3F4109D935FAA1F02BAFC9449EEE69F4A3917671BAEB4026C776`

Git HEAD at reset: `05bd58f` (history retained).

The snapshot includes uncommitted edits to `experiment-logs/README.md` and `no-cheat-progression-roadmap.md`, and the untracked Cohort 13 report. These are preserved in addition to the committed history.

To inspect or recover, extract to a separate directory and compare before restoring individual files. Do not extract over the new working tree. The archive contains source and documentation, not a live server/database backup.
