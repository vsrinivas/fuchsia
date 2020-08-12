// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/affine/transform.h>
#include <lib/zx/clock.h>
#include <zircon/syscalls/clock.h>

#include <array>

#include <zxtest/zxtest.h>

namespace {

// Unpack a zx_clock_transformation_t from a syscall result and put it into an
// affine::Transform so we can call methods on it.
inline affine::Transform UnpackTransform(const zx_clock_transformation_t& ct) {
  return affine::Transform{
      ct.reference_offset, ct.synthetic_offset, {ct.rate.synthetic_ticks, ct.rate.reference_ticks}};
}

// Unpack a zx_clock_rate_t from a syscall result and put it into an
// affine::Ratio so we can call methods on it.
inline affine::Ratio UnpackRatio(const zx_clock_rate_t& rate) {
  return affine::Ratio{rate.synthetic_ticks, rate.reference_ticks};
}

inline zx_status_t CreateClock(uint64_t options, zx::time backstop, zx::clock* result) {
  if (backstop.get() != 0) {
    zx_clock_create_args_v1 args;
    args.backstop_time = backstop.get();
    return zx::clock::create(options, &args, result);
  }

  return zx::clock::create(options, nullptr, result);
}

TEST(KernelClocksTestCase, Create) {
  zx::clock clock;

  // Creating a clock with no special options should succeed.
  ASSERT_OK(zx::clock::create(0, nullptr, &clock));

  // Creating a monotonic clock should succeed.
  ASSERT_OK(zx::clock::create(ZX_CLOCK_OPT_MONOTONIC, nullptr, &clock));

  // Creating a monotonic + continuous clock should succeed.
  ASSERT_OK(zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &clock));

  // Creating a continuous clock, but failing to say that it is also
  // monotonic, should fail.  The arguments are invalid.
  ASSERT_STATUS(zx::clock::create(ZX_CLOCK_OPT_CONTINUOUS, nullptr, &clock), ZX_ERR_INVALID_ARGS);

  // Attempting to create a clock with any currently undefined option flags
  // should fail.  The arguments are invalid.
  constexpr uint64_t ILLEGAL_OPTION = static_cast<uint64_t>(1) << (ZX_CLOCK_ARGS_VERSION_SHIFT - 1);
  static_assert((ZX_CLOCK_OPTS_ALL & ILLEGAL_OPTION) == 0, "Illegal option is actually legal!");
  ASSERT_STATUS(zx::clock::create(ILLEGAL_OPTION, nullptr, &clock), ZX_ERR_INVALID_ARGS);

  // Creating a clock with a defined, legal, backstop should work
  zx_clock_create_args_v1 args;
  args.backstop_time = 12345;
  ASSERT_OK(zx::clock::create(0, &args, &clock));

  // Passing a backstop time which is less than 0 is illegal
  args.backstop_time = -12345;
  ASSERT_STATUS(zx::clock::create(0, &args, &clock), ZX_ERR_INVALID_ARGS);

  // Note: the following tests require bypassing ulib/zx.  zx::clock_create Will
  // not allow us to make these mistakes.

  // Passing an args struct without specifying its version should fail.
  ASSERT_STATUS(zx_clock_create(0, &args, clock.reset_and_get_address()), ZX_ERR_INVALID_ARGS);

  // Passing no args struct with a valid version should also fail.
  ASSERT_STATUS(zx_clock_create(ZX_CLOCK_ARGS_VERSION(1), nullptr, clock.reset_and_get_address()),
                ZX_ERR_INVALID_ARGS);

  // Passing an invalid args version should fail.
  ASSERT_STATUS(zx_clock_create(ZX_CLOCK_ARGS_VERSION(7), &args, clock.reset_and_get_address()),
                ZX_ERR_INVALID_ARGS);
}

TEST(KernelClocksTestCase, Read) {
  zx::clock the_clock;
  int64_t read_val;

  constexpr std::array BACKSTOPS = {zx::time(0), zx::time(12345)};

  for (const auto backstop : BACKSTOPS) {
    // Create a basic clock, apply an explicit backstop value if needed.
    ASSERT_OK(CreateClock(0, backstop, &the_clock));

    // Attempt to read the clock.  It has never been set before, so it should
    // report the backstop time.
    ASSERT_OK(the_clock.read(&read_val));
    ASSERT_EQ(backstop.get(), read_val);

    // Wait a bit and try again.  It should still read zero; synthetic clocks do
    // not start to tick until after their first update.
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
    ASSERT_OK(the_clock.read(&read_val));
    ASSERT_EQ(backstop.get(), read_val);

    // Set the clock to a time.  Record clock monotonic before and after we
    // perform the initial update operation.  While we cannot control the exact
    // time at which the set operation will take place, we can bound the range
    // of possible transformations and establish a min and max.
    constexpr zx::time INITIAL_VALUE{1'000'000};
    zx::clock::update_args args;
    args.set_value(INITIAL_VALUE);

    zx::time before_update = zx::clock::get_monotonic();
    ASSERT_OK(the_clock.update(args));
    zx::time after_update = zx::clock::get_monotonic();

    // Now read the clock, and make sure that the value we read makes sense
    // given our bounds.
    zx::time before_read = zx::clock::get_monotonic();
    ASSERT_OK(the_clock.read(&read_val));
    zx::time after_read = zx::clock::get_monotonic();

    // Compute the minimum and maximum values we should be able to get from our
    // read operation based on the various bounds we have established.
    affine::Transform min_function{after_update.get(), INITIAL_VALUE.get(), {}};
    affine::Transform max_function{before_update.get(), INITIAL_VALUE.get(), {}};
    int64_t min_expected = min_function.Apply(before_read.get());
    int64_t max_expected = max_function.Apply(after_read.get());

    ASSERT_GE(read_val, min_expected);
    ASSERT_LE(read_val, max_expected);

    // Remove the READ rights from the clock, then verify that we can no longer read the clock.
    ASSERT_OK(the_clock.replace(ZX_DEFAULT_CLOCK_RIGHTS & ~ZX_RIGHT_READ, &the_clock));
    ASSERT_EQ(the_clock.read(&read_val), ZX_ERR_ACCESS_DENIED);
  }
}

TEST(KernelClocksTestCase, GetDetails) {
  // Create clocks with the default backstop of zero, and an explicit backstop
  constexpr std::array BACKSTOPS = {zx::time(0), zx::time(12345)};

  // Create the 3 types of clocks (basic, monotonic, and monotonic +
  // continuous), then make sure that get_details behaves properly for each
  // clock type as we update the clocks.
  std::array OPTIONS{
      static_cast<uint64_t>(0),
      ZX_CLOCK_OPT_MONOTONIC,
      ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
  };

  for (const auto backstop : BACKSTOPS) {
    for (const auto options : OPTIONS) {
      // Create the clock
      zx::clock the_clock;
      ASSERT_OK(CreateClock(options, backstop, &the_clock));

      ////////////////////////////////////////////////////////////////////////
      //
      // Phase 1: Fetch the initial details
      //
      ////////////////////////////////////////////////////////////////////////
      zx_clock_details_v1_t details;
      zx::ticks get_details_before = zx::ticks::now();
      ASSERT_OK(the_clock.get_details(&details));
      zx::ticks get_details_after = zx::ticks::now();

      // Check the generation counter.  It does not have a defined starting
      // value, but it should always be even.  An odd generation counter
      // indicates a clock which is in the process of being updates (something
      // we should never see when querying details)
      ASSERT_TRUE((details.generation_counter & 0x1) == 0);

      // The options reported should match those used to create the clock.
      ASSERT_EQ(options, details.options);

      // The backstop reported should match that used to create the clock (or be
      // 0 if the defaults were used).
      ASSERT_EQ(backstop.get(), details.backstop_time);

      // The |query_ticks| field of the details should indicate that this
      // clock was queried sometime between the before and after times latched
      // above.
      ASSERT_GE(details.query_ticks, get_details_before.get());
      ASSERT_LE(details.query_ticks, get_details_after.get());

      // The error bound should default to "unknown"
      ASSERT_EQ(ZX_CLOCK_UNKNOWN_ERROR, details.error_bound);

      // None of the dynamic properties of the clock have ever been set.
      // Their last update times should be 0.
      ASSERT_EQ(0, details.last_value_update_ticks);
      ASSERT_EQ(0, details.last_rate_adjust_update_ticks);
      ASSERT_EQ(0, details.last_error_bounds_update_ticks);

      // Both initial transformations should indicate that the clock has never
      // been set.  This is done by setting the numerator of the
      // transformation to 0, effectively stopping the synthetic clock.
      ASSERT_EQ(0, details.ticks_to_synthetic.rate.synthetic_ticks);
      ASSERT_EQ(0, details.mono_to_synthetic.rate.synthetic_ticks);

      // Record the details we just observed so we can observe how they change
      // as we update.
      zx_clock_details_v1_t last_details = details;

      ////////////////////////////////////////////////////////////////////////
      //
      // Phase 2: Set the initial value of the clock, then sanity check the
      // details.
      //
      ////////////////////////////////////////////////////////////////////////
      constexpr zx::time INITIAL_VALUE{1'000'000};
      zx::ticks update_before = zx::ticks::now();
      ASSERT_OK(the_clock.update(zx::clock::update_args{}.set_value(INITIAL_VALUE)));
      zx::ticks update_after = zx::ticks::now();

      get_details_before = zx::ticks::now();
      ASSERT_OK(the_clock.get_details(&details));
      get_details_after = zx::ticks::now();

      // Sanity check the query time
      ASSERT_GE(details.query_ticks, get_details_before.get());
      ASSERT_LE(details.query_ticks, get_details_after.get());

      // The generation counter should have incremented by exactly 2.
      ASSERT_EQ(last_details.generation_counter + 2, details.generation_counter);

      // The options should not have changed.
      ASSERT_EQ(options, details.options);

      // The error bound should still be "unknown"
      ASSERT_EQ(ZX_CLOCK_UNKNOWN_ERROR, details.error_bound);

      // The last value update time should be between the ticks that we
      // latched above.  Since this was the initial clock set operation, the
      // last rate adjustment time should update as well.  Even though we
      // didn't request it explicitly, the rate did go from stopped to
      // running.
      ASSERT_GE(details.last_value_update_ticks, update_before.get());
      ASSERT_LE(details.last_value_update_ticks, update_after.get());
      ASSERT_EQ(details.last_value_update_ticks, details.last_rate_adjust_update_ticks);
      ASSERT_EQ(details.last_value_update_ticks, details.last_rate_adjust_update_ticks);
      ASSERT_EQ(last_details.last_error_bounds_update_ticks,
                details.last_error_bounds_update_ticks);

      // The synthetic clock offset for both transformations should be the
      // initial value we set for the clock.
      ASSERT_EQ(INITIAL_VALUE.get(), details.ticks_to_synthetic.synthetic_offset);
      ASSERT_EQ(INITIAL_VALUE.get(), details.mono_to_synthetic.synthetic_offset);

      // The rate of the mono <-> synthetic transformation should be 1:1.  We
      // have not adjusted its rate yet, and its nominal rate is the same as
      // clock monotonic.
      ASSERT_EQ(1, details.mono_to_synthetic.rate.synthetic_ticks);
      ASSERT_EQ(1, details.mono_to_synthetic.rate.reference_ticks);

      // The expected ticks reference should be the update time.
      //
      // Note: this validation behavior assumes a particular behavior of the
      // kernel's update implementation.  Technically, there are many valid
      // solutions for computing this equation; the two offsets allow us to
      // write the equation for a line many different ways.  Even so, we
      // expect the kernel to be using the method we validate here because it
      // is simple, cheap, and precise.
      ASSERT_EQ(details.last_value_update_ticks, details.ticks_to_synthetic.reference_offset);

      // The rate of the ticks <-> synthetic should be equal to the ticks to
      // clock monotonic ratio.  Right now, however, we don't have a good way
      // to query the VDSO constants in order to find this ratio.  Instead, we
      // take it on faith that this is correct, then use the ratio to compute
      // and check the mono <-> synthetic reference offset.
      //
      // TODO(johngro): consider exposing this ratio from a VDSO based
      // syscall.
      int64_t expected_mono_reference;
      affine::Ratio ticks_to_mono = UnpackRatio(details.ticks_to_synthetic.rate);
      expected_mono_reference = ticks_to_mono.Scale(details.ticks_to_synthetic.reference_offset);
      ASSERT_EQ(expected_mono_reference, details.mono_to_synthetic.reference_offset);

      // Update the last_details and move on to the next phase.
      last_details = details;

      ////////////////////////////////////////////////////////////////////////
      //
      // Phase 3: Change the rate of the clock, then sanity check the details.
      //
      ////////////////////////////////////////////////////////////////////////
      constexpr int32_t PPM_ADJ = 65;
      update_before = zx::ticks::now();
      ASSERT_OK(the_clock.update(zx::clock::update_args{}.set_rate_adjust(PPM_ADJ)));
      update_after = zx::ticks::now();

      get_details_before = zx::ticks::now();
      ASSERT_OK(the_clock.get_details(&details));
      get_details_after = zx::ticks::now();

      // Sanity check the query time
      ASSERT_GE(details.query_ticks, get_details_before.get());
      ASSERT_LE(details.query_ticks, get_details_after.get());

      // The generation counter should have incremented by exactly 2.
      ASSERT_EQ(last_details.generation_counter + 2, details.generation_counter);

      // The options should not have changed.
      ASSERT_EQ(options, details.options);

      // The error bound should still be "unknown"
      ASSERT_EQ(ZX_CLOCK_UNKNOWN_ERROR, details.error_bound);

      // The last value and error bound update times should not have changed.
      // The last rate adjustment timestamp should be bounded by
      // update_before/update_after.
      ASSERT_EQ(last_details.last_value_update_ticks, details.last_value_update_ticks);
      ASSERT_EQ(last_details.last_error_bounds_update_ticks,
                details.last_error_bounds_update_ticks);
      ASSERT_GE(details.last_rate_adjust_update_ticks, update_before.get());
      ASSERT_LE(details.last_rate_adjust_update_ticks, update_after.get());

      // Validate the various transformation equations.
      //
      // Note: this validation behavior assumes a particular behavior of the
      // kernel.  Technically, there are many valid solutions for computing
      // this equation; the two offsets allow us to write the equation for a
      // line many different ways.  Even so, we expect the kernel to be using
      // the method we validate here because it is simple, cheap, and precise.
      //
      // If the behavior changes, there should be a Very Good Reason, and we
      // would like this test to break if someone decides to update the
      // methodology without updating the tests as well.

      // The expected synthetic clock offset for the transformations should be
      // the projected value of the last_rate_adjust_ticks time using the previous
      // transformation.
      int64_t expected_synth_offset;
      affine::Transform last_ticks_to_synth = UnpackTransform(last_details.ticks_to_synthetic);
      expected_synth_offset = last_ticks_to_synth.Apply(details.last_rate_adjust_update_ticks);

      ASSERT_EQ(expected_synth_offset, details.ticks_to_synthetic.synthetic_offset);
      ASSERT_EQ(expected_synth_offset, details.mono_to_synthetic.synthetic_offset);

      // The reference offset for ticks <-> synth should be the update time.
      // The reference for mono <-> synth should be the ticks reference
      // converted to mono.
      expected_mono_reference = ticks_to_mono.Scale(details.ticks_to_synthetic.reference_offset);
      ASSERT_EQ(expected_mono_reference, details.mono_to_synthetic.reference_offset);
      ASSERT_EQ(details.last_rate_adjust_update_ticks, details.ticks_to_synthetic.reference_offset);

      // Check our ratios.  We need to be a bit careful here; one cannot
      // simply compare ratios for equality without reducing them first.
      //
      // The mono <-> synth ratio should just be a function of the PPM
      // adjustment we applied.
      affine::Ratio expected_mono_ratio{1'000'000 + PPM_ADJ, 1'000'000};
      affine::Ratio actual_mono_ratio = UnpackRatio(details.mono_to_synthetic.rate);

      expected_mono_ratio.Reduce();
      actual_mono_ratio.Reduce();

      ASSERT_EQ(expected_mono_ratio.numerator(), actual_mono_ratio.numerator());
      ASSERT_EQ(expected_mono_ratio.denominator(), actual_mono_ratio.denominator());

      // The ticks <-> synth ratio should be the product of ticks to mono and
      // mono to synth.
      affine::Ratio expected_ticks_ratio = ticks_to_mono * expected_mono_ratio;
      affine::Ratio actual_ticks_ratio = UnpackRatio(details.ticks_to_synthetic.rate);

      expected_ticks_ratio.Reduce();
      actual_ticks_ratio.Reduce();

      ASSERT_EQ(expected_ticks_ratio.numerator(), actual_ticks_ratio.numerator());
      ASSERT_EQ(expected_ticks_ratio.denominator(), actual_ticks_ratio.denominator());

      // Update the last_details and move on to the next phase.
      last_details = details;

      ////////////////////////////////////////////////////////////////////////
      //
      // Phase 4: Update the error bound and verify that it sticks.  None
      // of the other core details should change.
      //
      ////////////////////////////////////////////////////////////////////////
      constexpr uint64_t ERROR_BOUND = 1234567;
      update_before = zx::ticks::now();
      ASSERT_OK(the_clock.update(zx::clock::update_args{}.set_error_bound(ERROR_BOUND)));
      update_after = zx::ticks::now();

      get_details_before = zx::ticks::now();
      ASSERT_OK(the_clock.get_details(&details));
      get_details_after = zx::ticks::now();

      // Sanity check the query time
      ASSERT_GE(details.query_ticks, get_details_before.get());
      ASSERT_LE(details.query_ticks, get_details_after.get());

      // The generation counter should have incremented by exactly 2.
      ASSERT_EQ(last_details.generation_counter + 2, details.generation_counter);

      // The options should not have changed.
      ASSERT_EQ(options, details.options);

      // The error bound should be what we set it to.
      ASSERT_EQ(ERROR_BOUND, details.error_bound);

      // The last value and rate adjust update times not have changed.  The
      // last error bound timestamp should be bounded by
      // update_before/update_after.
      ASSERT_EQ(last_details.last_value_update_ticks, details.last_value_update_ticks);
      ASSERT_EQ(last_details.last_rate_adjust_update_ticks, details.last_rate_adjust_update_ticks);
      ASSERT_GE(details.last_error_bounds_update_ticks, update_before.get());
      ASSERT_LE(details.last_error_bounds_update_ticks, update_after.get());

      // None of the transformations should have changed.
      auto CompareTransformation = [](const zx_clock_transformation_t& expected,
                                      const zx_clock_transformation_t& actual) {
        ASSERT_EQ(expected.reference_offset, expected.reference_offset);
        ASSERT_EQ(expected.synthetic_offset, expected.synthetic_offset);
        ASSERT_EQ(expected.rate.synthetic_ticks, expected.rate.synthetic_ticks);
        ASSERT_EQ(expected.rate.reference_ticks, expected.rate.reference_ticks);
      };

      ASSERT_NO_FAILURES(
          CompareTransformation(last_details.ticks_to_synthetic, details.ticks_to_synthetic));
      ASSERT_NO_FAILURES(
          CompareTransformation(last_details.mono_to_synthetic, details.mono_to_synthetic));

      ////////////////////////////////////////////////////////////////////////
      //
      // Phase 5: Make sure that attempt to fetch details fail when we mess up
      // things like the details structure version number, or the V1 structure
      // size.  Note that we need to bypass the zx::clock API for these tests.
      // As written, the zx::clock API will not allow us to make these mistakes.
      //
      ////////////////////////////////////////////////////////////////////////

      // Test a bad version number
      ASSERT_STATUS(
          zx_clock_get_details(the_clock.get_handle(), ZX_CLOCK_ARGS_VERSION(2), &details),
          ZX_ERR_INVALID_ARGS);

      // Test a bad pointer.
      ASSERT_STATUS(zx_clock_get_details(the_clock.get_handle(), ZX_CLOCK_ARGS_VERSION(1), nullptr),
                    ZX_ERR_INVALID_ARGS);

      // A buffer larger than strictly required should still work.
      uint8_t big_buffer[sizeof(details) + 8];
      ASSERT_OK(zx_clock_get_details(the_clock.get_handle(), ZX_CLOCK_ARGS_VERSION(1), big_buffer));

      ////////////////////////////////////////////////////////////////////////
      //
      // Phase 6: Finally, reduce the rights on the clock, discarding the READ
      // right in the process.  Make sure that we can no longer get_details.
      //
      ////////////////////////////////////////////////////////////////////////
      ASSERT_OK(the_clock.replace(ZX_DEFAULT_CLOCK_RIGHTS & ~ZX_RIGHT_READ, &the_clock));
      ASSERT_EQ(the_clock.get_details(&details), ZX_ERR_ACCESS_DENIED);
    }
  }
}

TEST(KernelClocksTestCase, Update) {
  zx::clock basic;
  zx::clock mono;
  zx::clock mono_cont;

  // Create three clocks.  A basic clock, a monotonic clock, and a monotonic
  // + continuous clock.
  ASSERT_OK(zx::clock::create(0, nullptr, &basic));
  ASSERT_OK(zx::clock::create(ZX_CLOCK_OPT_MONOTONIC, nullptr, &mono));
  ASSERT_OK(
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &mono_cont));

  // Set each clock to its initial value.  All clocks need to allow being
  // initially set, so this should be just fine.
  constexpr zx::time INITIAL_VALUE{1'000'000};
  zx::clock::update_args args;
  args.set_value(INITIAL_VALUE);

  ASSERT_OK(basic.update(args));
  ASSERT_OK(mono.update(args));
  ASSERT_OK(mono_cont.update(args));

  // Attempt to make each clock jump forward.  This should succeed for the
  // basic clock and the monotonic clock, but fail for the continuous clock
  // with 'invalid args'.  Note that this operation is timing sensitive.  If
  // the clocks are permitted to advance to the point that our value is no
  // longer in the future, then the monotonic set operation will fail as well.
  // To make sure this does not happen, make the jump be something enormous;
  // much larger than the maximum conceivable test watchdog timeout.  We use a
  // full day.
  constexpr zx::duration FWD_JUMP = zx::sec(86400);
  args.set_value(INITIAL_VALUE + FWD_JUMP);

  ASSERT_OK(basic.update(args));
  ASSERT_OK(mono.update(args));
  ASSERT_STATUS(mono_cont.update(args), ZX_ERR_INVALID_ARGS);

  // Attempt to make each clock jump backwards.  This should only succeed the
  // basic clock.  Neither flavor of monotonic should allow this.
  args.set_value(INITIAL_VALUE - zx::nsec(1));
  ASSERT_OK(basic.update(args));
  ASSERT_STATUS(mono.update(args), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(mono_cont.update(args), ZX_ERR_INVALID_ARGS);

  // Test rate adjustments.  All clocks should permit rate adjustment, but the
  // legal rate adjustment is fixed.
  struct RateTestVector {
    int32_t adj;
    zx_status_t expected_result;
  };
  constexpr std::array RATE_TEST_VECTORS = {
      RateTestVector{0, ZX_OK},
      RateTestVector{ZX_CLOCK_UPDATE_MIN_RATE_ADJUST, ZX_OK},
      RateTestVector{ZX_CLOCK_UPDATE_MAX_RATE_ADJUST, ZX_OK},
      RateTestVector{ZX_CLOCK_UPDATE_MIN_RATE_ADJUST - 1, ZX_ERR_INVALID_ARGS},
      RateTestVector{ZX_CLOCK_UPDATE_MAX_RATE_ADJUST + 1, ZX_ERR_INVALID_ARGS},
  };

  for (const auto& v : RATE_TEST_VECTORS) {
    args.reset().set_rate_adjust(v.adj);
    ASSERT_STATUS(basic.update(args), v.expected_result);
    ASSERT_STATUS(mono.update(args), v.expected_result);
    ASSERT_STATUS(mono_cont.update(args), v.expected_result);
  }

  // Test error bound reporting.  Error bounds are just information which is
  // atomically updated while making adjustments to the clock.  The kernel
  // should permit any value for this.
  constexpr std::array ERROR_BOUND_VECTORS = {
      static_cast<uint64_t>(12345),
      std::numeric_limits<uint64_t>::min(),
      std::numeric_limits<uint64_t>::max(),
      ZX_CLOCK_UNKNOWN_ERROR,
  };

  for (const auto& err_bound : ERROR_BOUND_VECTORS) {
    args.reset().set_error_bound(err_bound);
    ASSERT_OK(basic.update(args));
    ASSERT_OK(mono.update(args));
    ASSERT_OK(mono_cont.update(args));
  }

  // Attempt to set an illegal option for update option.  This should always
  // fail, but we have to by pass the zx::clock API in order to test this.
  // zx::clock will not allow this mistake to be made..
  constexpr uint64_t ILLEGAL_OPTION = 0x80000000;
  static_assert((ZX_CLOCK_UPDATE_OPTIONS_ALL & ILLEGAL_OPTION) == 0,
                "Illegal opt is actually legal!");

  zx_clock_update_args_v1_t update_args;
  uint64_t options = ZX_CLOCK_ARGS_VERSION(1) | ILLEGAL_OPTION;
  ASSERT_STATUS(zx_clock_update(basic.get_handle(), options, &update_args), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(zx_clock_update(mono.get_handle(), options, &update_args), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(zx_clock_update(mono_cont.get_handle(), options, &update_args),
                ZX_ERR_INVALID_ARGS);

  // Attempt to pass an invalid version number for the update argument struct.
  options = ZX_CLOCK_ARGS_VERSION(2);
  ASSERT_STATUS(zx_clock_update(basic.get_handle(), options, &update_args), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(zx_clock_update(mono.get_handle(), options, &update_args), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(zx_clock_update(mono_cont.get_handle(), options, &update_args),
                ZX_ERR_INVALID_ARGS);

  // Attempt to pass a bad pointer update argument struct.
  ASSERT_STATUS(zx_clock_update(basic.get_handle(), options, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(zx_clock_update(mono.get_handle(), options, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(zx_clock_update(mono_cont.get_handle(), options, nullptr), ZX_ERR_INVALID_ARGS);

  // Attempt to send an update command with no valid flags at all (eg; a
  // no-op).  This should also fail.
  args.reset();
  ASSERT_STATUS(basic.update(args), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(mono.update(args), ZX_ERR_INVALID_ARGS);
  ASSERT_STATUS(mono_cont.update(args), ZX_ERR_INVALID_ARGS);

  // Remove the WRITE rights from the basic clock handle, then verify that we
  // can no longer update it.
  args.reset().set_rate_adjust(0);
  ASSERT_OK(basic.replace(ZX_DEFAULT_CLOCK_RIGHTS & ~ZX_RIGHT_WRITE, &basic));
  ASSERT_STATUS(basic.update(args), ZX_ERR_ACCESS_DENIED);
}

TEST(KernelClocksTestCase, Backstop) {
  constexpr zx::time INITIAL_VALUE{zx::sec(86400).get()};
  constexpr zx::time BACKSTOP{12345};
  zx::clock the_clock;
  zx_time_t read_val;

  // Create a simple clock with an explicit backstop time
  ASSERT_OK(CreateClock(0, BACKSTOP, &the_clock));

  // Attempt to perform an initial set of the clock which would violate the
  // backstop.  This should fail.
  zx::clock::update_args args;
  args.set_value(BACKSTOP - zx::nsec(1));
  ASSERT_STATUS(the_clock.update(args), ZX_ERR_INVALID_ARGS);

  // The clock should still be at its backstop value and not advancing because
  // the initial set failed.
  ASSERT_OK(the_clock.read(&read_val));
  ASSERT_EQ(BACKSTOP.get(), read_val);

  zx::nanosleep(zx::deadline_after(zx::msec(10)));
  ASSERT_OK(the_clock.read(&read_val));
  ASSERT_EQ(BACKSTOP.get(), read_val);

  // Set the clock to a valid initial value.  This should succeed.
  args.set_value(INITIAL_VALUE);
  ASSERT_OK(the_clock.update(args));

  // Attempt to roll the clock back to before the backstop.  This should fail,
  // and the clock should still have a value >= the initial value we set.
  args.set_value(BACKSTOP - zx::nsec(1));
  ASSERT_STATUS(the_clock.update(args), ZX_ERR_INVALID_ARGS);

  ASSERT_OK(the_clock.read(&read_val));
  ASSERT_GE(read_val, INITIAL_VALUE.get());

  // Roll the clock all of the way back to the backstop.  We did not declare the
  // clock to be monotonic, so this should be OK.
  args.set_value(BACKSTOP);
  ASSERT_OK(the_clock.update(args));

  // The clock now must be >= the backstop value we set.  Also check to be sure
  // that it is <= the initial value set.  While this is technically a race and
  // _could_ flake, we chose an initial value of 24hrs and an initial backstop
  // of 12.345 uSec.  If the test framework takes more than 24 hrs - 12 uSec
  // to get from the update operation to this clock read operation (and has not
  // timed out in the process), then we have other more serious issues.
  ASSERT_OK(the_clock.read(&read_val));
  ASSERT_GE(read_val, BACKSTOP.get());
  ASSERT_LT(read_val, INITIAL_VALUE.get());
}

TEST(KernelClocksTestCase, StartedSignal) {
  //  Make a simple clock.
  zx::clock clock;
  ASSERT_OK(zx::clock::create(0, nullptr, &clock));

  // Wait up to 50msec for the clock to become started.  This should time out,
  // and the pending signals should come back as nothing.
  zx_signals_t pending = 0;
  ASSERT_STATUS(clock.wait_one(ZX_CLOCK_STARTED, zx::deadline_after(zx::msec(50)), &pending),
                ZX_ERR_TIMED_OUT);
  ASSERT_EQ(pending, 0);

  // Now go ahead and start the clock running.
  zx::clock::update_args args;
  ASSERT_OK(clock.update(args.set_value(zx::time(0))));

  // This time, our wait should succeed and the pending signal should indicate
  // ZX_CLOCK_STARTED.  No timeout should be needed.  This clock should already
  // be started.
  ASSERT_OK(clock.wait_one(ZX_CLOCK_STARTED, zx::time(0), &pending));
  ASSERT_EQ(pending, ZX_CLOCK_STARTED);
}

TEST(KernelClocksTestCase, DefaultRights) {
  //  Make a simple clock.
  zx::clock clock;
  ASSERT_OK(zx::clock::create(0, nullptr, &clock));

  // Fetch the basic info from the object.  It tell us the object's current rights.
  zx_info_handle_basic_t basic_info;
  size_t count;
  ASSERT_OK(clock.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), &count, nullptr));
  ASSERT_EQ(1, count);

  // Make sure that the default rights match what we expect.
  ASSERT_EQ(ZX_DEFAULT_CLOCK_RIGHTS, basic_info.rights);
}

TEST(KernelClocksTestCase, AutoStarted) {
  // Perform this test 3 times, one for each of the valid combinations of the
  // clock behavior flags.  IOW - no guarantees, a guarantee of monotonicity,
  // but not continuity, and a guarantee of both monotonicity and continuity.
  constexpr std::array BASE_CREATE_OPTIONS = {
      static_cast<uint64_t>(0),
      ZX_CLOCK_OPT_MONOTONIC,
      ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
  };

  constexpr zx::time the_dawn_of_time_itself{0};
  zx::time one_year_from_now = zx::deadline_after(zx::hour(1) * 24 * 365);
  zx::clock clock;

  for (const auto base_create_option : BASE_CREATE_OPTIONS) {
    // Create a simple clock, but specify that it should start ticking
    // automatically.
    ASSERT_OK(zx::clock::create(base_create_option | ZX_CLOCK_OPT_AUTO_START, nullptr, &clock));

    // Fetch the details for the clock.  Our mono <-> synth transformation
    // should be identity.
    zx_clock_details_v1_t details;
    ASSERT_OK(clock.get_details(&details));
    ASSERT_EQ(details.mono_to_synthetic.reference_offset,
              details.mono_to_synthetic.synthetic_offset);
    ASSERT_EQ(details.mono_to_synthetic.rate.reference_ticks,
              details.mono_to_synthetic.rate.synthetic_ticks);
    ASSERT_NE(0, details.mono_to_synthetic.rate.reference_ticks);

    // We omitted the create args structure, so the backstop time should be 0.
    ASSERT_EQ(0, details.backstop_time);

    // Make sure that the "started" signal has already been set for this clock
    // (since we auto-started it)
    zx_signals_t pending = 0;
    ASSERT_OK(clock.wait_one(ZX_CLOCK_STARTED, zx::time(0), &pending));
    ASSERT_EQ(pending, ZX_CLOCK_STARTED);

    // Spot check the ticks <-> synth transformation by making sure that a
    // simple read (which uses the ticks <-> synth transform under the hood) is
    // properly bracketed by before and after observations of clock monotonic.
    zx::time before = zx::clock::get_monotonic();
    zx::time now;
    ASSERT_OK(clock.read(now.get_address()));
    zx::time after = zx::clock::get_monotonic();
    ASSERT_LE(before, now);
    ASSERT_GE(after, now);

    // Check the various set options.  We should always be able set the rate of
    // the clock, as well as the error bound.  Setting the value of the clock,
    // however, is a limited operation.  With no restrictions, we should be able
    // to set the clock to anything, all of the way back to the backstop.  For a
    // monotonic clock, we should not be able to go backwards, but forwards
    // should be fine.  For a continuous clock, neither backwards nor forwards
    // should be OK.
    zx::clock::update_args args;
    switch (base_create_option) {
      case 0:
        ASSERT_OK(clock.update(args.reset().set_value(the_dawn_of_time_itself)));
        ASSERT_OK(clock.update(args.reset().set_value(one_year_from_now)));
        break;

      case ZX_CLOCK_OPT_MONOTONIC:
        ASSERT_STATUS(ZX_ERR_INVALID_ARGS,
                      clock.update(args.reset().set_value(the_dawn_of_time_itself)));
        ASSERT_OK(clock.update(args.reset().set_value(one_year_from_now)));
        break;

      case ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS:
        ASSERT_STATUS(ZX_ERR_INVALID_ARGS,
                      clock.update(args.reset().set_value(the_dawn_of_time_itself)));
        ASSERT_STATUS(ZX_ERR_INVALID_ARGS, clock.update(args.reset().set_value(one_year_from_now)));
        break;

      default:
        // this should never happen unless someone changes the list at the start
        ZX_ASSERT(false);
        break;
    }

    // Now check to be sure we can adjust the rate and the error bound.
    // Provided that we have write access to the clock, this should always be
    // OK.
    ASSERT_OK(clock.update(args.reset().set_rate_adjust(35)));
    ASSERT_OK(clock.update(args.reset().set_error_bound(100000)));
  }

  // Finally, attempt to create an auto-started clock, but specify a backstop
  // time which is ahead of the current clock monotonic.  This should fail since
  // we are asking for a clock which is supposed to start as a clone of clock
  // monotonic, but whose current time would violate the backstop time.
  //
  // Note: since monotonic is ticking forward while we are attempting to pick a
  // backstop time ahead of it, this is technically a race in the test.  Pick a
  // backstop time which is ~1 year ahead of now.  The assumption here is that
  // for the race to be lost, this test would need to go out to lunch for about
  // a year, which should trip the test framework's stuck-test timeout well
  // before the race is lost.
  zx_clock_create_args_v1 create_args = {.backstop_time =
                                             zx::deadline_after(zx::sec(86400ull * 365)).get()};
  ASSERT_STATUS(ZX_ERR_INVALID_ARGS,
                zx::clock::create(ZX_CLOCK_OPT_AUTO_START, &create_args, &clock));
}

TEST(KernelClocksTestCase, TrivialRateUpdates) {
  // Perform this test a number of times for different combinations of clock
  // creation arguments.
  constexpr std::array BASE_CREATE_OPTIONS = {
      static_cast<uint64_t>(0),
      ZX_CLOCK_OPT_MONOTONIC,
      ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
      ZX_CLOCK_OPT_AUTO_START,
      ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC,
      ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
  };

  for (const auto base_create_option : BASE_CREATE_OPTIONS) {
    zx::clock clock;

    // Create a simple clock, and if it was not configure to auto start, start
    // it now at an arbitrary time.
    ASSERT_OK(zx::clock::create(base_create_option, nullptr, &clock));
    if (!(base_create_option & ZX_CLOCK_OPT_AUTO_START)) {
      ASSERT_OK(clock.update(zx::clock::update_args().set_value(zx::time(12345678))));
    }

    // Test trivial rate updates over a range of different adjustments.
    constexpr std::array TEST_RATES = {0, 1, -1, 20, -20};
    for (int32_t rate_adj : TEST_RATES) {
      zx_clock_details_v1_t last_details, curr_details;
      zx_ticks_t before_update_ticks, after_update_ticks;

      zx::clock::update_args args;
      args.set_rate_adjust(rate_adj);

      // Set the rate to the rate we are going to test.  We can skip this for
      // the initial test rate adjustment of 0 ppm since newly created clocks
      // should always start with an initial rate adjustment of 0.
      if (rate_adj != 0) {
        ASSERT_OK(clock.update(args));
      }

      // Snapshot the clock's current details before our trivial update.
      ASSERT_OK(clock.get_details(&last_details));

      // Perform the update, paying attention to the tick time before and after
      // the update request.
      before_update_ticks = zx_ticks_get();
      ASSERT_OK(clock.update(args));
      after_update_ticks = zx_ticks_get();

      // Get the details after the update and compare them to before.  We expect
      // to see that only two things have changed:
      //
      // 1) The last_rate_adjustment ticks
      // 2) The generation counter
      ASSERT_OK(clock.get_details(&curr_details));

      // These should all be unchanged
      ASSERT_EQ(last_details.options, curr_details.options);
      ASSERT_EQ(last_details.backstop_time, curr_details.backstop_time);
      ASSERT_EQ(last_details.ticks_to_synthetic.reference_offset,
                curr_details.ticks_to_synthetic.reference_offset);
      ASSERT_EQ(last_details.ticks_to_synthetic.synthetic_offset,
                curr_details.ticks_to_synthetic.synthetic_offset);
      ASSERT_EQ(last_details.ticks_to_synthetic.rate.reference_ticks,
                curr_details.ticks_to_synthetic.rate.reference_ticks);
      ASSERT_EQ(last_details.ticks_to_synthetic.rate.synthetic_ticks,
                curr_details.ticks_to_synthetic.rate.synthetic_ticks);
      ASSERT_EQ(last_details.mono_to_synthetic.reference_offset,
                curr_details.mono_to_synthetic.reference_offset);
      ASSERT_EQ(last_details.mono_to_synthetic.synthetic_offset,
                curr_details.mono_to_synthetic.synthetic_offset);
      ASSERT_EQ(last_details.mono_to_synthetic.rate.reference_ticks,
                curr_details.mono_to_synthetic.rate.reference_ticks);
      ASSERT_EQ(last_details.mono_to_synthetic.rate.synthetic_ticks,
                curr_details.mono_to_synthetic.rate.synthetic_ticks);
      ASSERT_EQ(last_details.error_bound, curr_details.error_bound);
      ASSERT_EQ(last_details.last_value_update_ticks, curr_details.last_value_update_ticks);
      ASSERT_EQ(last_details.last_error_bounds_update_ticks,
                curr_details.last_error_bounds_update_ticks);

      // The last rate adjustment time should indicate a time between before/after_update_ticks
      ASSERT_LE(before_update_ticks, curr_details.last_rate_adjust_update_ticks);
      ASSERT_GE(after_update_ticks, curr_details.last_rate_adjust_update_ticks);

      // The generation counter should have incremented by exactly 2.
      ASSERT_EQ(last_details.generation_counter + 2, curr_details.generation_counter);
    }
  }
}

}  // namespace
