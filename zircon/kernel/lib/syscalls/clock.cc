// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/clock_dispatcher.h>

#include "priv.h"

namespace {
constexpr uint64_t GetArgsVersion(uint64_t options) {
  return (options & ZX_CLOCK_ARGS_VERSION_MASK) >> ZX_CLOCK_ARGS_VERSION_SHIFT;
}
}  // namespace

zx_status_t sys_clock_create(uint64_t options, user_in_ptr<const void> user_args,
                             user_out_handle* clock_out) {
  KernelHandle<ClockDispatcher> clock_handle;
  zx_clock_create_args_v1_t args{};
  zx_rights_t rights;
  zx_status_t result;

  // Extract the creation arguments based on the version signalled in options.
  switch (GetArgsVersion(options)) {
    // v0 implies "just use the defaults".  No args structure should have been
    // passed.  Just set our local v1 args structure to the default backstop
    // time of 0.
    case 0:
      if (user_args) {
        return ZX_ERR_INVALID_ARGS;
      }
      args.backstop_time = 0;
      break;

    // Extract the user args from the v1 structure.  They will be sanity checked
    // during the dispatcher's static Create
    case 1:
      result = user_args.reinterpret<const zx_clock_create_args_v1_t>().copy_from_user(&args);
      if (result != ZX_OK) {
        return result;
      }
      break;

    default:
      return ZX_ERR_INVALID_ARGS;
  }

  result = ClockDispatcher::Create(options, args, &clock_handle, &rights);
  if (result == ZX_OK) {
    result = clock_out->make(ktl::move(clock_handle), rights);
  }

  return result;
}

zx_status_t sys_clock_read(zx_handle_t clock_handle, user_out_ptr<zx_time_t> user_now) {
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<ClockDispatcher> clock;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(clock_handle, ZX_RIGHT_READ, &clock);
  if (status != ZX_OK) {
    return status;
  }

  zx_time_t now{};
  status = clock->Read(&now);
  if (status != ZX_OK) {
    return status;
  }

  return user_now.copy_to_user(now);
}

zx_status_t sys_clock_get_details(zx_handle_t clock_handle, uint64_t options,
                                  user_out_ptr<void> user_details) {
  // Currently, the only version of the details structure defined is V1.  If the
  // user failed to provide a buffer, or signaled a different version of the
  // structure, then it is an error.
  zx_clock_details_v1_t details{};
  if ((options != ZX_CLOCK_ARGS_VERSION(1)) || !user_details) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<ClockDispatcher> clock;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(clock_handle, ZX_RIGHT_READ, &clock);
  if (status != ZX_OK) {
    return status;
  }

  status = clock->GetDetails(&details);
  if (status != ZX_OK) {
    return status;
  }

  return user_details.reinterpret<zx_clock_details_v1>().copy_to_user(details);
}

zx_status_t sys_clock_update(zx_handle_t clock_handle, uint64_t options,
                             user_in_ptr<const void> user_args) {
  // Currently, the only version of the update structure defined is V1.  If the
  // user failed to provide a buffer, or signaled a different version of the
  // structure, then it is an error.
  zx_clock_update_args_v1_t args{};
  if ((GetArgsVersion(options) != 1) || !user_args) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status =
      user_args.reinterpret<const zx_clock_update_args_v1_t>().copy_from_user(&args);
  if (status != ZX_OK) {
    return status;
  }

  // Before going further, perform basic sanity checks of the update arguments.
  //
  // Only the defined options may be present in the request, and at least one of
  // them must be specified.
  options = options & ~ZX_CLOCK_ARGS_VERSION_MASK;
  if ((options & ~ZX_CLOCK_UPDATE_OPTIONS_ALL) || !(options & ZX_CLOCK_UPDATE_OPTIONS_ALL)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // The PPM adjustment must be within the legal range.
  if ((options & ZX_CLOCK_UPDATE_OPTION_RATE_ADJUST_VALID) &&
      ((args.rate_adjust < ZX_CLOCK_UPDATE_MIN_RATE_ADJUST) ||
       (args.rate_adjust > ZX_CLOCK_UPDATE_MAX_RATE_ADJUST))) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<ClockDispatcher> clock;
  status = up->handle_table().GetDispatcherWithRights(clock_handle, ZX_RIGHT_WRITE, &clock);
  if (status != ZX_OK) {
    return status;
  }

  return clock->Update(options, args);
}
