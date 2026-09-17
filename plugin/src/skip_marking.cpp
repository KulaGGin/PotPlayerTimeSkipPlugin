#include "plugin/skip_marking.hpp"

#include <utility>

#include "diagnostics/log.hpp"
#include "plugin/osd.hpp"
#include "plugin/player_window.hpp"
#include "plugin/skip_setup.hpp"

namespace plugin {

namespace {

// The full PTS-010/011/012 round trip a single committed range needs: open
// Skip Setup, make sure the feature is actually enabled (a range added
// while it's off would silently never skip anything), add the range, and
// OK the dialog. Cancels instead of OK-ing on any failure, so a
// half-verified add is never left sitting in an open dialog.
bool CommitRangeLive(const core::SkipRange& range) {
    const auto dialog = OpenSkipSetup();
    if (!dialog) {
        LOG_ERROR("skip-marking: could not open Skip Setup to commit [{}, {})", range.StartMs(),
                   range.EndMs());
        return false;
    }

    bool ok = EnsureSkipEnabled(*dialog) && AddFileSpecificSkipRange(*dialog, range);
    if (ok) {
        ok = CloseSkipSetupOk(*dialog);
    } else {
        CloseSkipSetupCancel(*dialog);
    }
    return ok;
}

}  // namespace

SkipMarkingDriver MakeLiveSkipMarkingDriver() {
    return SkipMarkingDriver{
        .isFileOpen = &IsFileOpen,
        .getPositionMs = &GetPositionMs,
        .commitRange = &CommitRangeLive,
        .showOsd = &ShowOsdLive,
    };
}

SkipMarkingStateMachine::SkipMarkingStateMachine(SkipMarkingDriver driver) : driver_(std::move(driver)) {}

void SkipMarkingStateMachine::StartNewEntry() {
    active_ = ActiveEntry{};
}

void SkipMarkingStateMachine::OnAltA() {
    if (!driver_.isFileOpen()) {
        LOG_WARN("skip-marking: Alt+A ignored, no file open");
        driver_.showOsd(ComposeOsdText({OsdEvent::kNoFileOpen}));
        return;
    }
    StartNewEntry();
    LOG_INFO("skip-marking: Alt+A, new entry started");
    driver_.showOsd(ComposeOsdText({OsdEvent::kNewMark}));
}

void SkipMarkingStateMachine::OnAltOpenBracket() {
    if (!driver_.isFileOpen()) {
        LOG_WARN("skip-marking: Alt+[ ignored, no file open");
        driver_.showOsd(ComposeOsdText({OsdEvent::kNoFileOpen}));
        return;
    }
    if (!active_) {
        StartNewEntry();
    }

    const core::Milliseconds pos = driver_.getPositionMs();
    if (active_->endMs && pos >= *active_->endMs) {
        LOG_WARN("skip-marking: Alt+[ at {} would not be before the set end {}, ignored", pos,
                  *active_->endMs);
        driver_.showOsd(ComposeOsdText({OsdEvent::kMarkIgnored}));
        return;
    }

    active_->startMs = pos;
    LOG_INFO("skip-marking: Alt+[, start set to {}", pos);
    if (!TryCommit()) {
        driver_.showOsd(ComposeOsdText({OsdEvent::kMarkStart, pos}));
    }
}

void SkipMarkingStateMachine::OnAltCloseBracket() {
    if (!driver_.isFileOpen()) {
        LOG_WARN("skip-marking: Alt+] ignored, no file open");
        driver_.showOsd(ComposeOsdText({OsdEvent::kNoFileOpen}));
        return;
    }
    if (!active_) {
        StartNewEntry();
    }

    const core::Milliseconds pos = driver_.getPositionMs();
    if (active_->startMs && pos <= *active_->startMs) {
        LOG_WARN("skip-marking: Alt+] at {} would not be after the set start {}, ignored", pos,
                  *active_->startMs);
        driver_.showOsd(ComposeOsdText({OsdEvent::kMarkIgnored}));
        return;
    }

    active_->endMs = pos;
    LOG_INFO("skip-marking: Alt+], end set to {}", pos);
    if (!TryCommit()) {
        driver_.showOsd(ComposeOsdText({OsdEvent::kMarkEnd, std::nullopt, pos}));
    }
}

bool SkipMarkingStateMachine::TryCommit() {
    if (!active_->startMs || !active_->endMs) {
        return false;
    }

    const core::SkipRange range = core::SkipRange::Create(*active_->startMs, *active_->endMs);
    if (driver_.commitRange(range)) {
        LOG_INFO("skip-marking: committed [{}, {})", range.StartMs(), range.EndMs());
        driver_.showOsd(ComposeOsdText({OsdEvent::kSaved, range.StartMs(), range.EndMs()}));
    } else {
        LOG_ERROR("skip-marking: failed to commit [{}, {})", range.StartMs(), range.EndMs());
        driver_.showOsd(ComposeOsdText({OsdEvent::kSaveFailed}));
    }
    active_.reset();
    return true;
}

}  // namespace plugin
