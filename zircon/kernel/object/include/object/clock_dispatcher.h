// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CLOCK_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CLOCK_DISPATCHER_H_

#include <lib/affine/transform.h>
#include <lib/kconcurrent/copy.h>
#include <lib/kconcurrent/seqlock.h>
#include <lib/relaxed_atomic.h>
#include <sys/types.h>
#include <zircon/rights.h>
#include <zircon/syscalls/clock.h>
#include <zircon/types.h>

#include <object/dispatcher.h>
#include <object/handle.h>

class ClockDispatcher final : public SoloDispatcher<ClockDispatcher, ZX_DEFAULT_CLOCK_RIGHTS> {
 public:
  static zx_status_t Create(uint64_t options, const zx_clock_create_args_v1_t& create_args,
                            KernelHandle<ClockDispatcher>* handle, zx_rights_t* rights);

  ~ClockDispatcher() final;
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_CLOCK; }

  zx_status_t Read(zx_time_t* out_now);
  zx_status_t GetDetails(zx_clock_details_v1_t* out_details);

  template <typename UpdateArgsType>
  zx_status_t Update(uint64_t options, const UpdateArgsType& args);

 private:
  struct Params {
    affine::Transform mono_to_synthetic{0, 0, {0, 1}};
    uint64_t error_bound = ZX_CLOCK_UNKNOWN_ERROR;
    zx_ticks_t last_value_update_ticks = 0;
    zx_ticks_t last_rate_adjust_update_ticks = 0;
    zx_ticks_t last_error_bounds_update_ticks = 0;
    uint32_t generation_counter_ = 0;
    int32_t cur_ppm_adj = 0;
  };

  ClockDispatcher(uint64_t options, zx_time_t backstop_time);

  bool is_monotonic() const { return (options_ & ZX_CLOCK_OPT_MONOTONIC) != 0; }
  bool is_continuous() const { return (options_ & ZX_CLOCK_OPT_CONTINUOUS) != 0; }
  bool is_started() const TA_REQ(seq_lock_) {
    // Note, we require that we hold the seq_lock_ exclusively here.  This
    // should ensure that there are no other threads writing to this memory
    // location concurrent with our read, meaning there is no formal data race
    // here.
    return params_.unsynchronized_get().mono_to_synthetic.numerator() != 0;
  }

  const uint64_t options_;
  const zx_time_t backstop_time_;

  // The transformation "payload" parameters, and the sequence lock which protects them.
  //
  // Note that the ticks_to_synthetic transformation is kept separate from the
  // rest of the parameters.  While we need to observe all of the parameters
  // during a call to GetDetails, we only need to observe ticks_to_synthetic
  // during Read, and keeping the parameters separate makes this a bit easier.
  DECLARE_SEQLOCK(ClockDispatcher) seq_lock_;
  TA_GUARDED(seq_lock_)
  concurrent::WellDefinedCopyable<affine::Transform> ticks_to_synthetic_{0, 0, affine::Ratio{0, 1}};
  concurrent::WellDefinedCopyable<Params> params_ TA_GUARDED(seq_lock_);
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CLOCK_DISPATCHER_H_
