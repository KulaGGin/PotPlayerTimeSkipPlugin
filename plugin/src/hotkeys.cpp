#include "plugin/hotkeys.hpp"

namespace plugin {

std::optional<HotkeyAction> ResolveHotkeyAction(int id) {
    for (const auto& definition : kHotkeyDefinitions) {
        if (definition.id == id) {
            return definition.action;
        }
    }
    return std::nullopt;
}

}
