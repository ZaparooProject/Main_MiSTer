#pragma once
#include <stdint.h>

// Keep startup black without trapping legacy/fallback frontends indefinitely.
// Live bus ownership has no timeout: only child teardown can end that phase.
namespace zaparoo_scanout {
class Bootstrap {
	bool hidden_ = false;
	bool fallback_ = false;
	uint32_t started_ = 0;
	uint32_t timeout_ = 0;
public:
	constexpr void start(bool eligible, uint32_t now)
	{
		hidden_ = eligible;
		fallback_ = false;
		started_ = now;
		timeout_ = 8000;
	}
	constexpr void cancel() { hidden_ = false; fallback_ = false; timeout_ = 0; }
	constexpr bool hidden() const { return hidden_; }
	constexpr void grant() { timeout_ = 0; }
	// Give fb0 a paint opportunity and let Main distinguish child exit from
	// a live fallback before revealing the legacy route.
	constexpr void fallback(uint32_t now)
	{
		if (hidden_ && !fallback_) { fallback_ = true; started_ = now; timeout_ = 250; }
	}
	constexpr bool expire(uint32_t now)
	{
		if (!hidden_ || !timeout_ || uint32_t(now - started_) < timeout_) return false;
		cancel();
		return true;
	}
};

// These execute in the supported ARM build, without an x86 Main executable.
constexpr bool bootstrap_policy_checks()
{
	Bootstrap b;
	b.start(false, 10);
	if (b.hidden()) return false;
	b.start(true, 10);
	if (!b.hidden() || b.expire(8009) || !b.expire(8010)) return false;
	b.start(true, 20);
	b.grant();
	if (b.expire(90000) || !b.hidden()) return false;
	b.fallback(30);
	b.fallback(200); // Repeated polling must not postpone recovery.
	if (b.expire(279) || !b.expire(280)) return false;
	b.start(true, 300);
	if (!b.hidden()) return false;
	b.cancel();
	if (b.hidden()) return false;
	b.start(true, UINT32_MAX - 1000);
	return !b.expire(0) && b.expire(8000);
}
static_assert(bootstrap_policy_checks(), "black startup/fallback lifetime");
}
