// clean_limits.cpp — TEST INPUT, intended to be correct (false-positive check).
#include <algorithm>
#include <cmath>
#include <optional>

struct JointLimits {
  double min_pos_rad;
  double max_pos_rad;
};

// Returns a position command clamped to [min_pos_rad, max_pos_rad],
// or std::nullopt if the input is not finite or the limits are invalid.
// Caller treats std::nullopt as a fault and commands a controlled stop.
// No allocation, no exceptions, no I/O: safe to call from the RT thread.
inline std::optional<double> clampPositionCommand(double cmd_rad,
                                                  const JointLimits& lim) noexcept {
  if (!std::isfinite(cmd_rad) ||
      !std::isfinite(lim.min_pos_rad) ||
      !std::isfinite(lim.max_pos_rad) ||
      lim.min_pos_rad > lim.max_pos_rad) {
    return std::nullopt;
  }
  return std::clamp(cmd_rad, lim.min_pos_rad, lim.max_pos_rad);
}
