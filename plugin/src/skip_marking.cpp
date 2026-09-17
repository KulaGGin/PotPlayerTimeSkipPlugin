#include "plugin/skip_marking.hpp"

#include <utility>

#include "diagnostics/log.hpp"
#include "plugin/osd.hpp"
#include "plugin/player_window.hpp"
#include "plugin/self_check.hpp"
#include "plugin/skip_setup.hpp"

namespace plugin {

namespace {

// The full PTS-010/011/012 round trip a single committed range needs: open
// Skip Setup, make sure the feature is actually enabled (a range added
// while it's off would silently never skip anything) unless PTS-016's
// config says not to auto-enable it, add the range, and OK the dialog.
// Cancels instead of OK-ing on any failure, so a half-verified add is never
// left sitting in an open dialog.
bool CommitRangeLive(const core::SkipRange& range, bool autoEnableSkip) {
    const auto dialog = OpenSkipSetup();
    if (!dialog) {
        LOG_ERROR("skip-marking: could not open Skip Setup to commit [{}, {})", range.StartMs(),
                   range.EndMs());
        return false;
    }

    bool ok = (!autoEnableSkip || EnsureSkipEnabled(*dialog)) && AddFileSpecificSkipRange(*dialog, range);
    if (ok) {
        ok = CloseSkipSetupOk(*dialog);
    } else {
        CloseSkipSetupCancel(*dialog);
    }
    return ok;
}

}  // namespace

SkipMarkingDriver MakeLiveSkipMarkingDriver(bool autoEnableSkip) {
    return SkipMarkingDriver{
        .checkVersionSupport = &EnsureSelfCheckPassed,
        .isFileOpen = &IsFileOpen,
        .getPositionMs = &GetPositionMs,
        .commitRange = [autoEnableSkip](const core::SkipRange& range) { return CommitRangeLive(range, autoEnableSkip); },
        .showOsd = &ShowOsdLive,
    };
}

SkipMarkingStateMachine::SkipMarkingStateMachine(SkipMarkingDriver driver) : driver_(std::move(driver)) {}

void SkipMarkingStateMachine::OnAltOpenBracket() {
    if (const auto unsupported = driver_.checkVersionSupport()) {
        LOG_ERROR("skip-marking: Alt+[ ignored, {}", *unsupported);
        driver_.showOsd(ComposeOsdText({OsdEvent::kUnsupportedVersion}));
        return;
    }
    if (!driver_.isFileOpen()) {
        LOG_WARN("skip-marking: Alt+[ ignored, no file open");
        driver_.showOsd(ComposeOsdText({OsdEvent::kNoFileOpen}));
        return;
    }

    const core::Milliseconds pos = driver_.getPositionMs();
    pending_.startMs = pos;
    LOG_INFO("skip-marking: Alt+[, pending start set to {}", pos);
    driver_.showOsd(ComposeOsdText({OsdEvent::kMarkStart, pos}));
}

void SkipMarkingStateMachine::OnAltCloseBracket() {
    if (const auto unsupported = driver_.checkVersionSupport()) {
        LOG_ERROR("skip-marking: Alt+] ignored, {}", *unsupported);
        driver_.showOsd(ComposeOsdText({OsdEvent::kUnsupportedVersion}));
        return;
    }
    if (!driver_.isFileOpen()) {
        LOG_WARN("skip-marking: Alt+] ignored, no file open");
        driver_.showOsd(ComposeOsdText({OsdEvent::kNoFileOpen}));
        return;
    }

    const core::Milliseconds pos = driver_.getPositionMs();
    pending_.endMs = pos;
    LOG_INFO("skip-marking: Alt+], pending end set to {}", pos);
    driver_.showOsd(ComposeOsdText({OsdEvent::kMarkEnd, std::nullopt, pos}));
}

void SkipMarkingStateMachine::OnAltA() {
    if (const auto unsupported = driver_.checkVersionSupport()) {
        LOG_ERROR("skip-marking: Alt+A ignored, {}", *unsupported);
        driver_.showOsd(ComposeOsdText({OsdEvent::kUnsupportedVersion}));
        return;
    }
    if (!driver_.isFileOpen()) {
        LOG_WARN("skip-marking: Alt+A ignored, no file open");
        driver_.showOsd(ComposeOsdText({OsdEvent::kNoFileOpen}));
        return;
    }
    if (!pending_.startMs || !pending_.endMs) {
        LOG_WARN("skip-marking: Alt+A ignored, start and/or end not set yet");
        driver_.showOsd(ComposeOsdText({OsdEvent::kMarkIncomplete}));
        return;
    }
    if (*pending_.endMs <= *pending_.startMs) {
        LOG_WARN("skip-marking: Alt+A ignored, pending end {} is not after pending start {}",
                  *pending_.endMs, *pending_.startMs);
        driver_.showOsd(ComposeOsdText({OsdEvent::kMarkIgnored}));
        return;
    }

    const core::SkipRange range = core::SkipRange::Create(*pending_.startMs, *pending_.endMs);
    if (driver_.commitRange(range)) {
        LOG_INFO("skip-marking: committed [{}, {})", range.StartMs(), range.EndMs());
        driver_.showOsd(ComposeOsdText({OsdEvent::kSaved, range.StartMs(), range.EndMs()}));
        pending_ = PendingMark{};
    } else {
        LOG_ERROR("skip-marking: failed to commit [{}, {})", range.StartMs(), range.EndMs());
        driver_.showOsd(ComposeOsdText({OsdEvent::kSaveFailed}));
    }
}

}  // namespace plugin
