// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/pid_control.h"

#include <lib/syslog/cpp/macros.h>

#include <cmath>

#include <gtest/gtest.h>

namespace media::audio::clock {
namespace {

class PidControlTest : public testing::Test {
 protected:
  static void VerifyProportionalOnly(const double pFactor) {
    auto control = PidControl(pFactor, 0, 0);
    control.Start(100);

    control.TuneForError(110, 50);
    EXPECT_FLOAT_EQ(control.Read(), 50 * pFactor);

    control.TuneForError(125, -10);
    EXPECT_FLOAT_EQ(control.Read(), -10 * pFactor);

    control.TuneForError(130, 20);
    EXPECT_FLOAT_EQ(control.Read(), 20 * pFactor);
  }

  static void VerifyIntegralOnly(const double iFactor) {
    auto control = PidControl(0, iFactor, 0);
    auto expected = 0.0;

    int64_t previous_time = 0;
    control.Start(previous_time);

    int64_t tune_time = previous_time + 10;
    // curr_err=50, dur=10: accum_err+=500
    control.TuneForError(tune_time, 50);

    // From this, we expect error to change by 50*t*I
    expected += 50.0 * iFactor * (tune_time - previous_time);
    EXPECT_FLOAT_EQ(control.Read(), expected);

    previous_time = tune_time;
    tune_time += 15;
    // curr_err=-100, dur=15: accum_err-=1500 (now -1000)
    control.TuneForError(tune_time, -100);

    // From this, we expect error to change by -100*t*I
    expected += -100.0 * iFactor * (tune_time - previous_time);
    EXPECT_FLOAT_EQ(control.Read(), expected);

    previous_time = tune_time;
    tune_time += 25;
    // curr_err=40, dur=25: accum_err+=1000 (now 0)
    control.TuneForError(tune_time, 40);

    // From this, we expect error to change by 0*t*I -- to be zero!
    expected += 40.0 * iFactor * (tune_time - previous_time);
    EXPECT_FLOAT_EQ(control.Read(), expected);
    EXPECT_FLOAT_EQ(expected, 0.0);
  }

  static void VerifyDerivativeOnly(double dFactor) {
    auto control = PidControl(0, 0, dFactor);
    double error, previous_error;
    double error_rate;

    int64_t previous_time, tune_time = 0;
    control.Start(tune_time);

    previous_time = tune_time;
    tune_time += 10;
    previous_error = 0;
    error = 50;
    // curr_err=50; prev_err=0; delta_err=50; dur=10; err_rate=50/10
    control.TuneForError(tune_time, error);

    error_rate = (error - previous_error) / (tune_time - previous_time);
    // Reset error to 0 at t=10, from here we expect error to change by 5
    EXPECT_FLOAT_EQ(control.Read(), dFactor * error_rate);

    previous_time = tune_time;
    tune_time += 5;
    previous_error = error;
    error = 15;
    // curr_err=20; prev_err=50; delta_err=-30; dur=5; err_rate=-30/5
    control.TuneForError(tune_time, error);

    error_rate = (error - previous_error) / (tune_time - previous_time);
    // Now we expect error to change by -6
    EXPECT_FLOAT_EQ(control.Read(), dFactor * error_rate);

    previous_time = tune_time;
    tune_time += 20;
    previous_error = error;
    error = 30;
    // curr_err=30; prev_err=20; delta_err=10; dur=20; err_rate=10/20
    control.TuneForError(tune_time, error);

    error_rate = (error - previous_error) / (tune_time - previous_time);
    // Now we expect error to change by 0.5
    EXPECT_FLOAT_EQ(control.Read(), dFactor * error_rate);
  }

  // This test uses time units of nanoseconds (as produced by the ZX_SEC macro).
  static void SmoothlyChaseToClockRate(const int32_t rate_adjust_ppm,
                                       const uint32_t num_iterations_limit) {
    // These PID factors were determined experimentally, from manual tuning and rule-of-thumb
    constexpr double kPfactor = 0.1;
    constexpr double kIfactor = kPfactor * 2 / ZX_MSEC(20);
    constexpr double kDfactor = kPfactor * ZX_MSEC(20) / 16;

    auto control = PidControl(kPfactor, kIfactor, kDfactor);

    constexpr zx_duration_t kIterationTimeslice = ZX_MSEC(10);
    const double ref_rate = (1'000'000 + rate_adjust_ppm) / 1'000'000.0;

    int64_t rate_change_mono_time = ZX_SEC(1);
    int64_t rate_change_ref_time = ZX_SEC(11);

    control.Start(0);
    control.TuneForError(rate_change_mono_time, 0);

    auto num_iterations = 0u;
    uint32_t first_accurate_prediction = UINT32_MAX;
    uint32_t consecutive_prediction = UINT32_MAX;
    bool previous_prediction_accurate = false;

    int64_t previous_ref_time = rate_change_ref_time;
    for (int64_t mono_time = rate_change_mono_time + ZX_MSEC(10); mono_time < ZX_SEC(2);
         mono_time += kIterationTimeslice) {
      ++num_iterations;

      auto predict_ppm = static_cast<int64_t>(round(control.Read()));
      predict_ppm = std::max<int64_t>(std::min<int64_t>(predict_ppm, +1000), -1000);

      if (predict_ppm == rate_adjust_ppm) {
        if (previous_prediction_accurate && consecutive_prediction > num_iterations) {
          consecutive_prediction = num_iterations;
          break;
        }
        previous_prediction_accurate = true;
        if (first_accurate_prediction > num_iterations) {
          first_accurate_prediction = num_iterations;
        }
      } else {
        previous_prediction_accurate = false;
      }

      int64_t predict_ref_time =
          previous_ref_time + (kIterationTimeslice * (1'000'000 + predict_ppm)) / 1'000'000;
      int64_t ref_time = rate_change_ref_time + (mono_time - rate_change_mono_time) * ref_rate;

      control.TuneForError(mono_time, ref_time - predict_ref_time);
      previous_ref_time = predict_ref_time;
    }

    EXPECT_LE(first_accurate_prediction, num_iterations_limit - 3)
        << "PidControl took too long to initially settle";
    EXPECT_LE(consecutive_prediction, num_iterations_limit)
        << "PidControl took too long to finally settle";
  }
};

// Validate default ctor sends parameters of 0
TEST_F(PidControlTest, Static) {
  auto control = PidControl();
  EXPECT_EQ(control.Read(), 0);

  control.Start(100);
  EXPECT_EQ(control.Read(), 0);

  control.TuneForError(125, 500);
  EXPECT_EQ(control.Read(), 0);
}

// If only Proportional, after each Tune we predict exactly that that error.
TEST_F(PidControlTest, Proportional) {
  VerifyProportionalOnly(1.0);
  VerifyProportionalOnly(0.5);
  VerifyProportionalOnly(0.01);
}

// If only Integral, after each Tune we predict based on accumulated error over time
TEST_F(PidControlTest, Integral) {
  VerifyIntegralOnly(1.0);
  VerifyIntegralOnly(0.2);
  VerifyIntegralOnly(0.001);
}

// If only Derivative, after each Tune we predict based on the change in error
TEST_F(PidControlTest, Derivative) {
  VerifyDerivativeOnly(1.0);
  VerifyDerivativeOnly(4.0);
  VerifyDerivativeOnly(0.0001);
}

// Start sets the control's initial time, resetting other vlaues to zero.
TEST_F(PidControlTest, NoStart) {
  auto control = PidControl(0, 0, 1.0);

  // start_time is implicitly 0, so control.Read will base its extrapolation(time=300)
  // across the previous tuning which had a duration of 150, thus control=(150-0)/(150-0) = 1.
  control.TuneForError(150, 150);
  EXPECT_EQ(control.Read(), 1);

  control.Start(100);
  // start_time is 100, so control.Read will base its extrapolation(time=300)
  // across the previous tuning which had a duration of 50, thus control=(150-0)/(150-100) = 3.
  control.TuneForError(150, 150);
  EXPECT_EQ(control.Read(), 3);
}

// Briefly validate PI with literal values
TEST_F(PidControlTest, ProportionalIntegral) {
  auto control = PidControl(1.0, 1.0, 0);

  control.Start(0);
  // Expect 0, was 50: curr_err_=50, dur=10: accum_err+=500 (now 500)
  control.TuneForError(10, 50);

  // From this  we expect error (50+500)=550
  EXPECT_EQ(control.Read(), 550);

  // Expect 550, was 500: curr_err=-50, dur=15: accum_err-=750 (now -250)
  control.TuneForError(25, -50);

  // From this, we expect error -50-250)=-300
  EXPECT_EQ(control.Read(), -300);

  // Expect -300, was -250: curr_err=50, dur=25: accum_err+=1250 (now 1000)
  control.TuneForError(50, 50);

  // From this, we expect error 50+1000=1050
  EXPECT_EQ(control.Read(), 1050);
}

// Briefly validate full PID with literal values
TEST_F(PidControlTest, FullPid) {
  auto control = PidControl(1.0, 1.0, 1.0);

  control.Start(0);
  // curr_err_=50, dur=10: accum_err+=500 (now 500)
  // prev_err=0; delta_err=50; err_rate=50/10=5
  control.TuneForError(10, 50);

  // Now expect error 50+500+5
  EXPECT_EQ(control.Read(), 555);  // 50 + 500 + 5

  // curr_err=-200 (for example, expected output 600 but actual 400), dur=10:
  // accum_err-=2000 (now -1500) prev_err=50; delta_err=-250; err_rate=-250/10=-25
  control.TuneForError(20, -200);

  // Now expect error -200-1500-25
  EXPECT_EQ(control.Read(), -1725);  // -200 + -1500 - 25

  // curr_err=50, dur=25: accum_err+=1250 (now -250)
  // prev_err=-200; delta_err=250; err_rate= 250/25=10
  control.TuneForError(45, 50);

  // Now expect error 50-1000+10
  EXPECT_EQ(control.Read(), -190);  // 50 - 250 + 10
}

TEST_F(PidControlTest, RealWorld) {
  SmoothlyChaseToClockRate(+1, 6);
  SmoothlyChaseToClockRate(-1, 6);

  SmoothlyChaseToClockRate(+10, 10);
  SmoothlyChaseToClockRate(-10, 10);

  SmoothlyChaseToClockRate(+100, 20);
  SmoothlyChaseToClockRate(-100, 20);

  SmoothlyChaseToClockRate(+950, 55);
  SmoothlyChaseToClockRate(-950, 55);
}

}  // namespace
}  // namespace media::audio::clock
