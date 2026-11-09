// Standby view (DESIGN.md §9): everything hidden, one dim dot drifting slowly
// so nothing static can burn in. Shown over the translator; the translator's
// own tree is hidden meanwhile.
#pragma once

namespace ui::standby {

void create();          // once, after the translator screen exists
void show(bool on);
void tick();            // moves the dot; call from loop
}  // namespace ui::standby
