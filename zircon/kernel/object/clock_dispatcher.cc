// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/affine/ratio.h>
#include <lib/affine/transform.h>
#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/syscalls/clock.h>

#include <fbl/alloc_checker.h>
#include <object/clock_dispatcher.h>

KCOUNTER(dispatcher_clock_create_count, "dispatcher.clock.create")
KCOUNTER(dispatcher_clock_destroy_count, "dispatcher.clock.destroy")

namespace {

inline zx_clock_transformation_t CopyTransform(const affine::Transform& src) {
  return {src.a_offset(), src.b_offset(), {src.numerator(), src.denominator()}};
}

// Helpers which normalize access to the two versions of the update args.
template <typename UpdateArgsType>
class UpdateArgsAccessor {
 public:
  static constexpr bool IsV1 = ktl::is_same_v<UpdateArgsType, zx_clock_update_args_v1_t>;
  static constexpr bool IsV2 = ktl::is_same_v<UpdateArgsType, zx_clock_update_args_v2_t>;

  UpdateArgsAccessor(const UpdateArgsType& args) : args_(args) {}
  int32_t rate_adjust() const { return args_.rate_adjust; }
  uint64_t error_bound() const { return args_.error_bound; }

  int64_t synthetic_value() const {
    if constexpr (IsV1) {
      return args_.value;
    } else {
      return args_.synthetic_value;
    }
  }

  // Reference value is an invalid field in the v1 struct.
  int64_t reference_value() const {
    static_assert(!IsV1, "v1 clock update structures have no reference value field");
    return args_.reference_value;
  }

 private:
  const UpdateArgsType& args_;
};

}  // namespace

zx_status_t ClockDispatcher::Create(uint64_t options, const zx_clock_create_args_v1_t& create_args,
                                    KernelHandle<ClockDispatcher>* handle, zx_rights_t* rights) {
  // The syscall_ layer has already parsed our args version and extracted them
  // into our |create_args| argument as appropriate.  Go ahead and discard the
  // version information before sanity checking the rest of the options.
  options &= ~ZX_CLOCK_ARGS_VERSION_MASK;

  // Reject any request which includes an options flag we do not recognize.
  if (~ZX_CLOCK_OPTS_ALL & options) {
    return ZX_ERR_INVALID_ARGS;
  }

  // If the user asks for a continuous clock, it must also be monotonic
  if ((options & ZX_CLOCK_OPT_CONTINUOUS) && !(options & ZX_CLOCK_OPT_MONOTONIC)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Make sure that the backstop time is valid.  If this clock is being created
  // with the "auto start" flag, then it begins life as a clone of clock
  // monotonic, and the backstop time has to be <= the current clock monotonic
  // value.  Otherwise, the clock starts in the stopped state, and any specified
  // backstop time must simply be non-negative.
  //
  if (((options & ZX_CLOCK_OPT_AUTO_START) && (create_args.backstop_time > current_time())) ||
      (create_args.backstop_time < 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  KernelHandle clock(fbl::AdoptRef(new (&ac) ClockDispatcher(options, create_args.backstop_time)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  *rights = default_rights();
  *handle = ktl::move(clock);
  return ZX_OK;
}

ClockDispatcher::ClockDispatcher(uint64_t options, zx_time_t backstop_time)
    : options_(options),
      backstop_time_(backstop_time),
      mono_to_synthetic_{0, backstop_time, {0, 1}},
      ticks_to_synthetic_{0, backstop_time, {0, 1}} {
  // If this clock is created with the "auto start" flag, set the clock up to
  // initially be a clone of clock monotonic instead of being in an undefined
  // (non-started) state.
  if (options & ZX_CLOCK_OPT_AUTO_START) {
    ZX_DEBUG_ASSERT(backstop_time <= current_time());  // This should have been checked by Create
    affine::Ratio ticks_to_mono_ratio = platform_get_ticks_to_time_ratio();
    mono_to_synthetic_ = {0, 0, {1, 1}},
    ticks_to_synthetic_ = {
        0, 0, {ticks_to_mono_ratio.numerator(), ticks_to_mono_ratio.denominator()}};
    UpdateState(0, ZX_CLOCK_STARTED);
  }

  kcounter_add(dispatcher_clock_create_count, 1);
}

ClockDispatcher::~ClockDispatcher() { kcounter_add(dispatcher_clock_destroy_count, 1); }

zx_status_t ClockDispatcher::Read(zx_time_t* out_now) {
  int64_t now_ticks;
  affine::Transform ticks_to_synthetic;

  while (true) {
    // load the generation counter.  If it is odd, we are in the middle of
    // an update and need to wait.  Just spin; the update operation (once
    // started) is non-preemptable and will be done very shortly.
    auto gen = gen_counter_.load(ktl::memory_order_acquire);
    if ((gen & 0x1) == 0) {
      // Latch the transformation and observe the tick counter.
      ticks_to_synthetic = ticks_to_synthetic_;
      now_ticks = current_ticks();

      // If the generation counter has not changed, then we are done.
      // Otherwise, we need to start over.
      if (gen == gen_counter_.load(ktl::memory_order_acquire)) {
        break;
      }
    }

    // Pause just a bit before trying again.
    arch::Yield();
  }

  *out_now = ticks_to_synthetic.Apply(now_ticks);

  return ZX_OK;
}

zx_status_t ClockDispatcher::GetDetails(zx_clock_details_v1_t* out_details) {
  while (true) {
    // load the generation counter.  If it is odd, we are in the middle of
    // an update and need to wait.  Just spin; the update operation (once
    // started) is non-preemptable and will be done very shortly.
    auto gen = gen_counter_.load(ktl::memory_order_acquire);
    if ((gen & 0x1) == 0) {
      // Latch the detailed information.
      out_details->generation_counter = gen;
      out_details->ticks_to_synthetic = CopyTransform(ticks_to_synthetic_);
      out_details->mono_to_synthetic = CopyTransform(mono_to_synthetic_);
      out_details->error_bound = error_bound_;
      out_details->query_ticks = current_ticks();
      out_details->last_value_update_ticks = last_value_update_ticks_;
      out_details->last_rate_adjust_update_ticks = last_rate_adjust_update_ticks_;
      out_details->last_error_bounds_update_ticks = last_error_bounds_update_ticks_;

      // If the generation counter has not changed, then we are done.
      // Otherwise, we need to start over.
      if (gen == gen_counter_.load(ktl::memory_order_acquire)) {
        break;
      }

      // Pause just a bit before trying again.
      arch::Yield();
    }
  }

  // Options and backstop_time are constant over the life of the clock.  We
  // don't need to latch them during the generation counter spin.
  out_details->options = options_;
  out_details->backstop_time = backstop_time_;

  return ZX_OK;
}

template <typename UpdateArgsType>
zx_status_t ClockDispatcher::Update(uint64_t options, const UpdateArgsType& _args) {
  const bool do_set = options & ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID;
  const bool do_rate = options & ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID;
  const bool reference_valid = options & ZX_CLOCK_UPDATE_OPTION_REFERENCE_VALUE_VALID;
  const UpdateArgsAccessor args(_args);

  static_assert((args.IsV1 || args.IsV2) && (args.IsV1 != args.IsV2),
                "Clock update arguments must be either version 1, or version 2");

  // Perform the v1/v2 parameter sanity checks that we can perform without being
  // in the writer lock.
  if constexpr (args.IsV1) {
    // v1 clocks are not allowed to specify a reference value (the v1 struct
    // does not have a field for it)
    if (reference_valid) {
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    static_assert(args.IsV2, "Unrecognized clock update args version!");

    // A reference value may only be provided during a V2 update as part of
    // either a value set, or rate change operation (or both).
    if (reference_valid && !do_set && !do_rate) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  bool clock_was_started = false;
  {
    // Enter the writer lock.  Only one update can take place at a time.  We use
    // an IrqSave spinlock for this because this operation should be very quick,
    // and we may have observers who are spinning attempting to read the clock.
    // We cannot afford to become preempted while we are performing an update
    // operation.
    Guard<SpinLock, IrqSave> writer_lock{&writer_lock_};

    // If the clock has not yet been started, then we require the first update
    // to include a set operation.
    if (!do_set && !is_started()) {
      return ZX_ERR_BAD_STATE;
    }

    // Continue with the argument sanity checking.  Set operations are not
    // allowed on continuous clocks after the very first one (which is what
    // starts the clock).
    if (do_set && is_continuous() && is_started()) {
      return ZX_ERR_INVALID_ARGS;
    }

    // Checks specific to non-V1 update arguments.
    if constexpr (!args.IsV1) {
      // The following checks only apply if the clock is a monotonic clock which
      // has already been started.
      if (is_started() && is_monotonic()) {
        // Set operations for non-V1 update arguments made to a monotonic clock
        // must supply an explicit reference time.
        if (do_set && !reference_valid) {
          return ZX_ERR_INVALID_ARGS;
        }

        // non-v1 set operations on monotonic clocks may not be combined with rate
        // change operations.  Additionally, rate change operations may not specify
        // an explicit reference time when being applied to monotonic clocks.
        if (is_monotonic() && (do_set || reference_valid) && do_rate) {
          return ZX_ERR_INVALID_ARGS;
        }
      }
    }

    // Mark the time at which this update will take place.
    int64_t now_ticks = static_cast<int64_t>(current_ticks());

    // Don't bother updating the structures representing the transformation if:
    //
    // 1) We are not changing either the value or rate, or
    // 2a) This is a rate-only change (the value is not being set)
    // 2b) With no explicit reference time provided
    // 2c) Which specifies the same rate that we are already using
    const bool skip_update =
        !do_set && (!do_rate || (!reference_valid && (args.rate_adjust() == cur_ppm_adj_)));

    // Now compute the new transformations
    affine::Transform m2s;
    affine::Transform t2s;
    if (!skip_update) {
      // Figure out the reference times at which this change will take place at.
      affine::Ratio ticks_to_mono_ratio = platform_get_ticks_to_time_ratio();
      int64_t now_mono = ticks_to_mono_ratio.Scale(now_ticks);
      int64_t reference_ticks = now_ticks;
      int64_t reference_mono = now_mono;
      if constexpr (!args.IsV1) {
        if (reference_valid) {
          reference_mono = args.reference_value();
          reference_ticks = ticks_to_mono_ratio.Inverse().Scale(reference_mono);
        }
      }

      // Next, figure out the synthetic value this clock will have after the
      // change.  If this is a set operation, it will be the explicit value
      // provided by the user, otherwise it will be the synthetic value computed
      // using the old transformation applied to the target reference time.
      //
      // In the case that we need to compute the target synthetic time from a
      // previous transformation, use the old mono->synthetic time
      // transformation if the user explicitly supplied a monotonic reference
      // time for the update operation.  Otherwise, use the old ticks->synthetic
      // time transformation along with reference ticks value which we observed
      // after entering the writer lock.
      //
      // In the case of a user supplied monotonic reference time, this avoids
      // rounding error ensures that the old and the new transformations both
      // pass through exactly the same [user_ref, synth] point (important during
      // testing).
      int64_t target_synthetic =
          do_set ? args.synthetic_value()
                 : (reference_valid ? mono_to_synthetic_.Apply(reference_mono)
                                    : ticks_to_synthetic_.Apply(reference_ticks));

      // Compute the new rate ratios.
      affine::Ratio new_m2s_ratio;
      affine::Ratio new_t2s_ratio;
      if (do_rate) {
        new_m2s_ratio = {static_cast<uint32_t>(1'000'000 + args.rate_adjust()), 1'000'000};
        new_t2s_ratio = ticks_to_mono_ratio * new_m2s_ratio;
      } else if (is_started()) {
        new_m2s_ratio = mono_to_synthetic_.ratio();
        new_t2s_ratio = ticks_to_synthetic_.ratio();
      } else {
        new_m2s_ratio = {1, 1};
        new_t2s_ratio = ticks_to_mono_ratio;
      }

      // Update the local copies of the structures.
      m2s = {reference_mono, target_synthetic, new_m2s_ratio};
      t2s = {reference_ticks, target_synthetic, new_t2s_ratio};

      // Make certain that the new transformations follow all of the rules
      // before applying them. In specific, we need to make certain that:
      //
      // 1) Monotonic clocks do not move backwards.
      // 2) Backstop times are not violated.
      //
      int64_t new_synthetic_now = t2s.Apply(now_ticks);
      if (is_monotonic() && (new_synthetic_now < ticks_to_synthetic_.Apply(now_ticks))) {
        return ZX_ERR_INVALID_ARGS;
      }

      if (new_synthetic_now < backstop_time_) {
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      m2s = mono_to_synthetic_;
      t2s = ticks_to_synthetic_;
    }

    // Everything checks out, we can proceed with the update.  Start by bumping
    // the generation counter.  This will disable all read operations until we
    // bump the counter again.
    //
    // We should only need release semantics here.  Only things which hold the
    // writer spin-lock can make changes to the generation counter, and acquiring
    // that spin-lock should serve as our acquire barrier.
    auto prev_counter = gen_counter_.fetch_add(1, ktl::memory_order_release);
    ZX_DEBUG_ASSERT((prev_counter & 0x1) == 0);

    // Update the transformations, and record whether or not this is the initial
    // start of the clock.
    clock_was_started = !is_started();
    mono_to_synthetic_ = m2s;
    ticks_to_synthetic_ = t2s;

    // If this was a set operation, record the new last update time.
    if (do_set) {
      last_value_update_ticks_ = now_ticks;
    }

    // If this was a rate adjustment operation, or the clock was just started,
    // record the new last update time as well as the new current ppm
    // adjustment.
    if (do_rate || clock_was_started) {
      last_rate_adjust_update_ticks_ = now_ticks;
      cur_ppm_adj_ = do_rate ? args.rate_adjust() : 0;
    }

    // If this was an error bounds update operations, record the new last update
    // time as well as the new error bound.
    if (options & ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID) {
      last_error_bounds_update_ticks_ = now_ticks;
      error_bound_ = args.error_bound();
    }

    // We are finished.  Update the generation counter to allow clock reading again.
    gen_counter_.store(prev_counter + 2, ktl::memory_order_release);
  }

  // Now that we are out of the time critical section, if the clock was just
  // started, make sure to assert the ZX_CLOCK_STARTED signal to observers.
  if (clock_was_started) {
    UpdateState(0, ZX_CLOCK_STARTED);
  }

  return ZX_OK;
}

// Explicit instantiation of the two types of update we might encounter.
template zx_status_t ClockDispatcher::Update(uint64_t options,
                                             const zx_clock_update_args_v1_t& args);
template zx_status_t ClockDispatcher::Update(uint64_t options,
                                             const zx_clock_update_args_v2_t& args);
