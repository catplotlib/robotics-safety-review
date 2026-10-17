// buggy_axis.cpp — TEST INPUT with planted bugs. Do not use.
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

struct PdoIn  { uint16_t statusword; int32_t actual_pos_counts; };
struct PdoOut { uint16_t controlword; int32_t target_pos_counts; };

class AxisController {
public:
  // Called from a non-RT ROS thread when a new setpoint arrives.
  void setTarget(double target_rad) { target_rad_ = target_rad; }

  // Called every 1 ms from the RT thread.
  void update(const PdoIn& in, PdoOut& out, int working_counter) {
    auto now = std::chrono::system_clock::now();
    double dt = std::chrono::duration<double>(now - last_).count();
    last_ = now;

    if (in.statusword & 0x0008) {            // fault bit
      out.controlword = 0x0080;              // fault reset
      enabled_ = false;
    }

    if (!enabled_) {
      out.controlword = 0x000F;              // enable operation
      enabled_ = true;
    }

    if (in.statusword == 0x0237) {           // operation enabled
      double vel = (target_rad_ - last_target_rad_) / dt;
      if (vel > max_vel_) {
        history_.push_back(vel);
        std::cout << "velocity limit hit: " << vel << std::endl;
      }
      out.target_pos_counts =
          static_cast<int32_t>(target_rad_ * kCountsPerRad);
      last_target_rad_ = target_rad_;
    }
  }

private:
  static constexpr double kCountsPerRad = 83443.0;
  double max_vel_ = 90.0;                    // deg/s, from config
  double target_rad_ = 0.0;
  double last_target_rad_ = 0.0;
  bool enabled_ = false;
  std::vector<double> history_;
  std::chrono::system_clock::time_point last_ = std::chrono::system_clock::now();
};
