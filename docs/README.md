# docs

Reference docs and research findings for this project.

[`FINDINGS.md`](FINDINGS.md) is the canonical, single source for every
measured/inferred fact about how PotPlayer stores and manages skip ranges
(storage format, the in-memory cache invariant, dialog control IDs, the
playback query interface, loader/proxy analysis, and cross-process
gotchas). Don't duplicate that detail elsewhere — link here instead.

PTS-005's proxy-viability verdict (whether PotPlayer accepts an unsigned
`MediaDB64.dll` proxy) will be recorded as an update to `FINDINGS.md` §5
once that spike runs.
