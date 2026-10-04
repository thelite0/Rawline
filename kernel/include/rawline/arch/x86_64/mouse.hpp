#pragma once

#include <rawline/pointer_event.hpp>

namespace rawline::kernel::arch::x86_64 {

bool initialize_mouse();
bool poll_mouse_event(pointer::Event* event);

}
