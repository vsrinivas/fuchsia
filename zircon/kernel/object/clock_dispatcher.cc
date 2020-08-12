// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <lib/affine/ratio.h>
#include <lib/affine/transform.h>
#include <lib/arch/intrin.h>
#include <lib/counters.h>
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

zx_status_t ClockDispatcher::Update(uint64_t options, const zx_clock_update_args_v1_t& args) {
  const bool do_set = options & ZX_CLOCK_UPDATE_OPTION_VALUE_VALID;
  const bool do_rate = options & ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID;

  // if this is a set operation, and we are trying to set the time to something
  // before the backstop, just deny the operation.  The backstop time is a fixed
  // property of the clock determined at creation time; we don't need to even
  // enter into the writer lock to know that this is an illegal operation.
  if (do_set && (args.value < backstop_time_)) {
    return ZX_ERR_INVALID_ARGS;
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

    // Bump the generation counter.  This will disable all read operations until
    // we bump the counter again.
    //
    // We should only need release semantics here.  Only things which hold the
    // writer spin-lock can make changes to the generation counter, and acquiring
    // that spin-lock should serve as our acquire barrier.
    auto prev_counter = gen_counter_.fetch_add(1, ktl::memory_order_release);
    ZX_DEBUG_ASSERT((prev_counter & 0x1) == 0);
    int64_t now_ticks = static_cast<int64_t>(current_ticks());

    // Are we updating the transformations at all?
    if (do_set || do_rate) {
      int64_t now_synthetic;

      // Figure out the new synthetic offset
      if (do_set) {
        // We are performing a set operation.  If this clock is started and
        // monotonic, and the set operation would result in non-monotonic
        // behavior for the clock, disallow it.
        if (is_started() && is_monotonic()) {
          int64_t now_clock = ticks_to_synthetic_.Apply(now_ticks);
          if (args.value < now_clock) {
            // turns out we are not going to make any changes to the clock.
            // Put the generation counter back to where it was.
            gen_counter_.store(prev_counter, ktl::memory_order_release);
            return ZX_ERR_INVALID_ARGS;
          }
        }

        // Because this is a set operation, now on the synthetic timeline is
        // what the user has specified.
        now_synthetic = args.value;
        last_value_update_ticks_ = now_ticks;

        // We are past the point where this update can fail.  All of our
        // parameters have been sanity checked, including the monotonic check
        // which can only be performed while we are holding off readers using
        // the generation counter.  At this point, because we are performing a
        // set operation, we are starting the clock if it was not already
        // started.
        clock_was_started = !is_started();
      } else {
        // Looks like we are updating the rate, but not explicitly setting the
        // clock.  Make sure that the offsets we choose for the new affine
        // transformation result in it being 1st order continuous with the
        // previous transformation.  The simple way to do this is to choose the
        // reference time at which the change is taking place (now_ticks) as the
        // first offset, and the same value transformed by the previous
        // transformation as the second (synthetic) offset.  By definition, this
        // point marks the point at which the previous transformation's domain
        // ended and the new one started, and must exist on the line defined by
        // both transformations.
        now_synthetic = ticks_to_synthetic_.Apply(now_ticks);
      }

      // Figure out the new rates.
      affine::Ratio ticks_to_mono_ratio = platform_get_ticks_to_time_ratio();
      affine::Ratio mono_to_synthetic_rate;
      affine::Ratio ticks_to_synthetic_rate;
      bool skip_update = false;

      if (do_rate) {
        // We want to explicitly update the rate.  Encode the PPM adjustment
        // as a ratio, then compute the ticks_to_synthetic_rate.
        //
        // If the PPM adjustment being applied is identical to the last
        // adjustment being applied, then don't bother to recompute these.  Just
        // use the rates we already have.
        if (args.rate_adjust != cur_ppm_adj_) {
          mono_to_synthetic_rate = {static_cast<uint32_t>(1000000 + args.rate_adjust), 1000000};
          ticks_to_synthetic_rate = ticks_to_mono_ratio * mono_to_synthetic_rate;
          cur_ppm_adj_ = args.rate_adjust;
        } else {
          mono_to_synthetic_rate = mono_to_synthetic_.ratio();
          ticks_to_synthetic_rate = ticks_to_synthetic_.ratio();

          // If our rate is being "adjusted" to the same thing that it already
          // was, and we are not updating the position at all, then we can just
          // go ahead and skip the update of the transformation equations (even
          // though we will record the time of this update as the last rate
          // adjustment time).  See fxb/57593
          skip_update = !do_set;
        }
        last_rate_adjust_update_ticks_ = now_ticks;
      } else if (!is_started()) {
        // The clock has never been started, then the default rate is 1:1
        // with the mono reference.
        mono_to_synthetic_rate = {1, 1};
        ticks_to_synthetic_rate = ticks_to_mono_ratio;
        last_rate_adjust_update_ticks_ = now_ticks;
      } else {
        // Otherwise, preserve the existing rate.
        mono_to_synthetic_rate = mono_to_synthetic_.ratio();
        ticks_to_synthetic_rate = ticks_to_synthetic_.ratio();
      }

      // Now, simply update the transformations with the proper offsets and
      // the calculated rates.
      if (!skip_update) {
        zx_time_t now_mono = ticks_to_mono_ratio.Scale(now_ticks);
        mono_to_synthetic_ = {now_mono, now_synthetic, mono_to_synthetic_rate};
        ticks_to_synthetic_ = {now_ticks, now_synthetic, ticks_to_synthetic_rate};
      }
    }

    // If we are supposed to update the error bound, do so.
    if (options & ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID) {
      error_bound_ = args.error_bound;
      last_error_bounds_update_ticks_ = now_ticks;
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
