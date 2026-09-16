#include "plugin/window.hpp"

namespace plugin {

std::optional<std::uintptr_t> SelectWindow(const std::vector<WindowCandidate>& candidates,
                                            std::wstring_view wantedClassName) {
    std::optional<std::uintptr_t> firstMatch;

    for (const auto& candidate : candidates) {
        if (candidate.className != wantedClassName) {
            continue;
        }
        if (candidate.visible) {
            return candidate.handle;
        }
        if (!firstMatch) {
            firstMatch = candidate.handle;
        }
    }

    return firstMatch;
}

std::optional<std::uintptr_t> SelectWindowByTitle(const std::vector<WindowCandidate>& candidates,
                                                   std::wstring_view wantedClassName,
                                                   std::wstring_view wantedTitle) {
    for (const auto& candidate : candidates) {
        if (candidate.className == wantedClassName && candidate.title == wantedTitle) {
            return candidate.handle;
        }
    }
    return std::nullopt;
}

}
