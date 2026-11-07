#pragma once

#include <fmt/core.h>

#include <cstdio>
#include <utility>

namespace utils {

// Small formatting helpers so the whole codebase logs consistently. Flush so
// VR/OpenXR debugging works even when stdout is redirected to a file.
template <typename... Args>
void logInfo(fmt::format_string<Args...> fmt, Args &&...args) {
	fmt::print("[info] ");
	fmt::print(fmt, std::forward<Args>(args)...);
	fmt::print("\n");
	std::fflush(stdout);
}

template <typename... Args>
void logWarn(fmt::format_string<Args...> fmt, Args &&...args) {
	fmt::print(stderr, "[warn] ");
	fmt::print(stderr, fmt, std::forward<Args>(args)...);
	fmt::print(stderr, "\n");
	std::fflush(stderr);
}

}        // namespace utils
