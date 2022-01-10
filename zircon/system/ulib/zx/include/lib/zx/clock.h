// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_CLOCK_H_
#define LIB_ZX_CLOCK_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#include <lib/zx/time.h>
#include <zircon/availability.h>
#include <zircon/syscalls/clock.h>

namespace zx {

class clock final : public object<clock> {
 public:
  class update_args {
   public:
    constexpr update_args() = default;

    update_args& reset() {
      options_ = 0;
      return *this;
    }

    update_args& set_value(zx::time value) {
      args_.synthetic_value = value.get();
      options_ |= ZX_CLOCK_UPDATE_OPTION_SYNTHETIC_VALUE_VALID;
      return *this;
    }

    update_args& set_reference_value(zx::time reference_value) {
      args_.reference_value = reference_value.get();
      options_ |= ZX_CLOCK_UPDATE_OPTION_REFERENCE_VALUE_VALID;
      return *this;
    }

    update_args& set_both_values(zx::time reference_value, zx::time synthetic_value) {
      args_.reference_value = reference_value.get();
      args_.synthetic_value = synthetic_value.get();
      options_ |= ZX_CLOCK_UPDATE_OPTION_BOTH_VALUES_VALID;
      return *this;
    }

    update_args& set_rate_adjust(int32_t rate) {
      args_.rate_adjust = rate;
      options_ |= ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID;
      return *this;
    }

    update_args& set_error_bound(uint64_t error_bound) {
      args_.error_bound = error_bound;
      options_ |= ZX_CLOCK_UPDATE_OPTION_ERROR_BOUND_VALID;
      return *this;
    }

   private:
    friend class ::zx::clock;
    static constexpr uint32_t kArgsVersion = 2u;
    zx_clock_update_args_v2_t args_{};
    uint64_t options_ = 0;
  } ZX_AVAILABLE_SINCE(7);

  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_CLOCK;

  // TODO(johngro) : remove this alias once we remove the static get method from
  // this class.  This static get method will no longer be needed once UTC
  // leaves the kernel, and "thread" time becomes fetch-able only from a
  // get_info request.  At that point in time, zx_clock_get will disappear and
  // the only kernel provided sources of time will be get_monotonic and ticks.
  zx_handle_t get_handle() const { return object_base::get(); }

  constexpr clock() = default;

  explicit clock(zx_handle_t value) : object(value) {}

  explicit clock(handle&& h) : object(h.release()) {}

  clock(clock&& other) : object(other.release()) {}

  clock& operator=(clock&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(uint64_t options, const zx_clock_create_args_v1* args, clock* result)
      ZX_AVAILABLE_SINCE(7) {
    options = (options & ~ZX_CLOCK_ARGS_VERSION_MASK) |
              ((args != nullptr) ? ZX_CLOCK_ARGS_VERSION(1) : 0);

    return zx_clock_create(options, args, result->reset_and_get_address());
  }

  zx_status_t read(zx_time_t* now_out) const ZX_AVAILABLE_SINCE(7) {
    return zx_clock_read(value_, now_out);
  }

  zx_status_t get_details(zx_clock_details_v1_t* details_out) const ZX_AVAILABLE_SINCE(7) {
    return zx_clock_get_details(value_, ZX_CLOCK_ARGS_VERSION(1), details_out);
  }

  zx_status_t update(const update_args& args) const ZX_AVAILABLE_SINCE(7) {
    uint64_t options = args.options_ | ZX_CLOCK_ARGS_VERSION(args.kArgsVersion);
    return zx_clock_update(value_, options, &args.args_);
  }

  static time get_monotonic() ZX_AVAILABLE_SINCE(7) { return time(zx_clock_get_monotonic()); }
} ZX_AVAILABLE_SINCE(7);

using unowned_clock = unowned<clock> ZX_AVAILABLE_SINCE(7);

}  // namespace zx

#endif  // LIB_ZX_CLOCK_H_
