// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CLOCK_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CLOCK_DISPATCHER_H_

#include <lib/affine/transform.h>
#include <sys/types.h>
#include <zircon/rights.h>
#include <zircon/syscalls/clock.h>
#include <zircon/types.h>

#include <object/dispatcher.h>
#include <object/handle.h>

class ClockDispatcher final
    : public SoloDispatcher<ClockDispatcher, ZX_DEFAULT_CLOCK_RIGHTS, ZX_CLOCK_STARTED> {
 public:
  static zx_status_t Create(uint64_t options, const zx_clock_create_args_v1_t& create_args,
                            KernelHandle<ClockDispatcher>* handle, zx_rights_t* rights);

  ~ClockDispatcher() final;
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_CLOCK; }

  zx_status_t Read(zx_time_t* out_now);
  zx_status_t GetDetails(zx_clock_details_v1_t* out_details);
  zx_status_t Update(uint64_t options, const zx_clock_update_args_v1_t& args);

 private:
  ClockDispatcher(uint64_t options, zx_time_t backstop_time);
  static bool is_started(const affine::Transform& transform) {
    return (transform.numerator() != 0);
  }

  bool is_monotonic() const { return (options_ & ZX_CLOCK_OPT_MONOTONIC) != 0; }
  bool is_continuous() const { return (options_ & ZX_CLOCK_OPT_CONTINUOUS) != 0; }
  bool is_started() const { return is_started(mono_to_synthetic_); }

  const uint64_t options_;
  const zx_time_t backstop_time_;

  DECLARE_SPINLOCK(ClockDispatcher) writer_lock_;
  ktl::atomic<uint32_t> gen_counter_{0};
  affine::Transform mono_to_synthetic_{0, 0, {0, 1}};
  affine::Transform ticks_to_synthetic_{0, 0, {0, 1}};
  uint64_t error_bound_ = ZX_CLOCK_UNKNOWN_ERROR;
  zx_ticks_t last_value_update_ticks_ = 0;
  zx_ticks_t last_rate_adjust_update_ticks_ = 0;
  zx_ticks_t last_error_bounds_update_ticks_ = 0;
  int32_t cur_ppm_adj_ = 0;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_CLOCK_DISPATCHER_H_
