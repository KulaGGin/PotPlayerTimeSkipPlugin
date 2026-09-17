#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Timecode / skip-range logic lands here (PTS-003, PTS-008). Kept free of
// <windows.h> so it stays unit-testable on its own and never grows a
// dependency on the live player.
namespace core {

int add(int lhs, int rhs);

using Milliseconds = std::int64_t;

// Thrown for every parsing/validation failure in this file. There is no
// silent fallback to a plausible-looking 00:00:00 anywhere in this library —
// bad input always surfaces as one of these, with a message naming what was
// wrong, never a swallowed error.
class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ms <-> PotPlayer dialog format "HH:MM:SS.mmm". Round-trips exactly.
//
// FormatTimecode rejects negative ms. ParseTimecode requires the exact
// shape produced by FormatTimecode: one or more digits for hours, then
// ':', exactly two digits for minutes (00-59), ':', exactly two digits for
// seconds (00-59), '.', exactly three digits for milliseconds. Anything
// else (missing fields, extra text, out-of-range minutes/seconds) throws
// ParseError rather than being coerced.
std::string FormatTimecode(Milliseconds ms);
Milliseconds ParseTimecode(std::string_view text);

// The display-friendly cut of FormatTimecode's own "HH:MM:SS.mmm" — just
// "HH:MM:SS", no sub-second precision. PTS-015's OSD text uses this so
// on-screen feedback reads as a plain timestamp rather than the dialog's
// exact-round-trip format. Same negative-ms rejection as FormatTimecode,
// since it's implemented in terms of it.
std::string FormatTimecodeShort(Milliseconds ms);

// A validated skip range [startMs, endMs) in milliseconds. The only way to
// get one is Create(), which throws ParseError on a negative start or a
// range that is not strictly forward (endMs <= startMs covers both the
// backwards and zero-length cases PotPlayer's .pbf length field can never
// represent, since length is unsigned and must be > 0).
class SkipRange {
public:
    static SkipRange Create(Milliseconds startMs, Milliseconds endMs);

    Milliseconds StartMs() const { return startMs_; }
    Milliseconds EndMs() const { return endMs_; }
    Milliseconds LengthMs() const { return endMs_ - startMs_; }

private:
    SkipRange(Milliseconds startMs, Milliseconds endMs);

    Milliseconds startMs_;
    Milliseconds endMs_;
};

// PotPlayer's Type combo in the Skip Interval Setup dialog: `1` is
// File-specific, the only value this plugin ever writes (see
// docs/FINDINGS.md section 1 — Overall's on-disk value was never confirmed).
inline constexpr int kFileSpecificType = 1;

// One `[PlaySkip]` line: "<index>=<type>*<start_ms>*<length_ms>". Field 3 is
// a *length*, not an end time — see docs/FINDINGS.md section 1, the
// signature failure mode this whole domain revolves around.
struct PbfEntry {
    int index;
    int type;
    SkipRange range;
};

// Serializes/parses a single non-terminator `[PlaySkip]` line, e.g.
// "0=1*754567*671111". SerializePbfLine never emits the terminator form;
// ParsePbfLine throws ParseError on the terminator line ("N=") and on any
// line that doesn't parse as "<int>=<int>*<int>*<int>" with a valid range.
std::string SerializePbfLine(const PbfEntry& entry);
PbfEntry ParsePbfLine(std::string_view line);

// Builds/parses a whole `[PlaySkip]` section as a pure string transform —
// UTF-16LE+BOM encoding is the I/O layer's job (docs/FINDINGS.md section 1),
// this only produces/consumes the text content, CRLF-terminated to match a
// real captured .pbf byte-for-byte.
//
// BuildPlaySkipSection emits "[PlaySkip]\r\n" followed by one line per
// range (type defaulting to kFileSpecificType) and the empty "N=\r\n"
// terminator PotPlayer itself always writes.
//
// ParsePlaySkipSection accepts that same shape: it skips a leading
// "[PlaySkip]" header line if present, is tolerant of the empty terminator
// line (silently ignored, not an error), and throws ParseError on any other
// malformed line.
std::string BuildPlaySkipSection(const std::vector<SkipRange>& ranges,
                                  int type = kFileSpecificType);
std::vector<SkipRange> ParsePlaySkipSection(std::string_view section);

// Lenient human-input parsing for tooling/config, as distinct from the
// strict dialog-format round trip above. Accepts "SS", "MM:SS", "HH:MM:SS",
// each with an optional ".mmm" suffix on the last field. The sole field of
// the "SS" form is unbounded (it names a total elapsed duration); minutes
// and seconds fields in the two- and three-field forms must each be in
// [0, 59]. Anything else — non-numeric text, empty input, wrong field
// counts, out-of-range fields — throws ParseError. Never coerces to 0.
Milliseconds ParseHumanTimecode(std::string_view text);

// Parses a human-entered range: two ParseHumanTimecode operands joined by
// one of the separators "->", "~", "-" (checked in that order, so "->" is
// matched whole before its leading '-' could be mistaken for the plain "-"
// separator), with optional surrounding whitespace. Delegates range
// validation to SkipRange::Create, so a backwards or zero-length range
// throws the same ParseError it would.
SkipRange ParseHumanRange(std::string_view text);

}
