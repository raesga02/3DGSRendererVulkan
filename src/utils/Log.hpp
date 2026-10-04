#pragma once

#include <fmt/core.h>

#include <utility>

namespace utils {

// Small formatting helpers so the whole codebase logs consistently.
template <typename... Args>
void logInfo(fmt::format_string<Args...> fmt, Args &&...args) {
	fmt::print("[info] ");
	fmt::print(fmt, std::forward<Args>(args)...);
	fmt::print("\n");
}

template <typename... Args>
void logWarn(fmt::format_string<Args...> fmt, Args &&...args) {
	fmt::print(stderr, "[warn] ");
	fmt::print(fmt, std::forward<Args>(args)...);
	fmt::print(stderr, "\n");
}

}        // namespace utils
