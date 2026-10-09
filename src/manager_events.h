#pragma once
#include <atomic>

// Internal UI wakeup, not a config revision. Producers only signal; the game
// thread consumes it before touching native UI. No plugin callbacks run here.
namespace manager_events {
inline std::atomic<bool> changed{false};
inline void notify() noexcept { changed.store(true, std::memory_order_release); }
}
