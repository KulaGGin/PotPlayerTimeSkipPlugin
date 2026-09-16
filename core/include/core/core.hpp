#pragma once

// Timecode / skip-range logic lands here (PTS-003). Kept free of
// <windows.h> so it stays unit-testable on its own and never grows a
// dependency on the live player.
namespace core {

int add(int lhs, int rhs);

}
