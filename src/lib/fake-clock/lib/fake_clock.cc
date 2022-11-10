// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.testing.deadline/cpp/wire.h>
#include <fidl/fuchsia.testing/cpp/wire.h>
#include <lib/component/cpp/incoming/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/port.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <thread>

#include <src/lib/fake-clock/named-timer/named_timer.h>

namespace fake_clock = fuchsia_testing;

namespace {
fidl::UnownedClientEnd<fake_clock::FakeClock> GetService() {
  static std::once_flag svc_connect_once;
  static fidl::ClientEnd<fake_clock::FakeClock> fake_clock;

  std::call_once(svc_connect_once, []() {
    if (!fake_clock.is_valid()) {
      zx::result result = component::Connect<fake_clock::FakeClock>();
      if (result.is_error()) {
        FX_PLOGS(ERROR, result.status_value())
            << "Failed to connect to fuchsia.testing.FakeClock service";
      }
      fake_clock = std::move(result.value());
    }
  });
  return fake_clock.borrow();
}

zx::eventpair MakeEvent(zx_time_t deadline) {
  zx::eventpair l, r;
  if (zx_status_t status = zx::eventpair::create(0, &l, &r); status != ZX_OK) {
    ZX_PANIC("%s", zx_status_get_string(status));
  }
  const fidl::WireResult result =
      fidl::WireCall(GetService())->RegisterEvent(std::move(r), deadline);
  ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  return l;
}

}  // namespace

__EXPORT zx_status_t zx_futex_wait(const zx_futex_t* value_ptr, zx_futex_t current_value,
                                   zx_handle_t new_futex_owner, zx_time_t deadline) {
  ZX_ASSERT_MSG(deadline == ZX_TIME_INFINITE,
                "zx_futex_wait with deadline is currently supported by FakeClock library");
  return _zx_futex_wait(value_ptr, current_value, new_futex_owner, deadline);
}

__EXPORT zx_status_t zx_channel_call(zx_handle_t handle, uint32_t options, zx_time_t deadline,
                                     const zx_channel_call_args_t* args, uint32_t* actual_bytes,
                                     uint32_t* actual_handles) {
  // TODO(brunodalbo) There may be a way to get channel_call working if we create a temporary
  // channel and an auxiliary thread, but looks like most channel_call call sites don't define
  // deadlines.
  ZX_ASSERT_MSG(deadline == ZX_TIME_INFINITE,
                "zx_channel_call with deadline is not yet supported by FakeClock library");
  return _zx_channel_call(handle, options, deadline, args, actual_bytes, actual_handles);
}

__EXPORT zx_time_t zx_clock_get_monotonic() {
  const fidl::WireResult result = fidl::WireCall(GetService())->Get();
  ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  return result.value().time;
}

__EXPORT zx_time_t zx_deadline_after(zx_duration_t duration) {
  return zx_time_add_duration(zx_clock_get_monotonic(), duration);
}

__EXPORT zx_status_t zx_nanosleep(zx_time_t deadline) {
  zx::eventpair e = MakeEvent(deadline);
  if (zx_status_t status =
          _zx_object_wait_one(e.get(), ZX_EVENTPAIR_SIGNALED, ZX_TIME_INFINITE, nullptr) != ZX_OK) {
    ZX_PANIC("%s", zx_status_get_string(status));
  }
  return ZX_OK;
}

// wait_one is implemented by making it a wait_many on an infinite deadline with two items: one is
// the original handle+signals, the other is the eventpair created from the fake-clock service.
__EXPORT zx_status_t zx_object_wait_one(zx_handle_t handle, zx_signals_t signals,
                                        zx_time_t deadline, zx_signals_t* observed) {
  if (deadline == ZX_TIME_INFINITE || deadline == 0) {
    return _zx_object_wait_one(handle, signals, deadline, observed);
  }
  zx::eventpair e = MakeEvent(deadline);
  zx_wait_item_t items[] = {
      {
          .handle = e.get(),
          .waitfor = ZX_EVENTPAIR_SIGNALED,
      },
      {
          .handle = handle,
          .waitfor = signals,
      },
  };

  zx_status_t status = _zx_object_wait_many(items, 2, ZX_TIME_INFINITE);
  if (observed) {
    *observed = items[1].pending;
  }
  if (status != ZX_OK) {
    return status;
  }
  if ((items[0].pending & ZX_EVENTPAIR_SIGNALED) != 0) {
    return ZX_ERR_TIMED_OUT;
  }
  return ZX_OK;
}

// wait_many is implemented by adding an extra eventpair handle extracted from fake-clock to the
// wait list, and changing the deadline to infinite. If the number of items on the wait is already
// ZX_WAIT_MANY_MAX_ITEMS (meaning we can't add an extra item), we create a port instead and
// register all the wait items to it.
__EXPORT zx_status_t zx_object_wait_many(zx_wait_item_t* items, size_t num_items,
                                         zx_time_t deadline) {
  if (deadline == ZX_TIME_INFINITE || deadline == 0 || num_items > ZX_WAIT_MANY_MAX_ITEMS) {
    return _zx_object_wait_many(items, num_items, deadline);
  }
  if (num_items == ZX_WAIT_MANY_MAX_ITEMS) {
    // can't add a new item, we need to build a port and wait on it.
    zx::port port;
    if (zx_status_t status = zx::port::create(0, &port); status != ZX_OK) {
      ZX_PANIC("%s", zx_status_get_string(status));
    }
    for (size_t i = 0; i < num_items; i++) {
      if (zx_status_t status =
              zx::unowned_handle(items[i].handle)->wait_async(port, i, items[i].waitfor, 0);
          status != ZX_OK) {
        return status;
      }
    }
    zx::eventpair event = MakeEvent(deadline);
    if (zx_status_t status = event.wait_async(port, num_items, ZX_EVENTPAIR_SIGNALED, 0);
        status != ZX_OK) {
      ZX_PANIC("%s", zx_status_get_string(status));
    }

    auto update_item = [&items, num_items](const zx_port_packet& packet) {
      if (packet.key == num_items) {
        if (packet.signal.observed & ZX_EVENTPAIR_SIGNALED) {
          return true;
        }
      } else {
        items[packet.key].pending = packet.signal.observed;
      }
      return false;
    };

    zx_port_packet_t packet;
    if (zx_status_t status = port.wait(zx::time::infinite(), &packet); status != ZX_OK) {
      return status;
    }
    // update_item will return true if the first packet out of the port is a timeout.
    if (update_item(packet)) {
      return ZX_ERR_TIMED_OUT;
    }
    // many things may have happened at once, how we just keep polling the port with a zero deadline
    // and updating the items
    while (port.wait(zx::time(0), &packet) == ZX_OK) {
      if (update_item(packet)) {
        break;
      }
    }
    return ZX_OK;
  }
  // we can just add an extra item, but we'll need to copy all the wait items
  zx_wait_item_t tmp[ZX_WAIT_MANY_MAX_ITEMS];
  std::copy_n(items, num_items, tmp);
  zx::eventpair event = MakeEvent(deadline);
  tmp[num_items].pending = 0;
  tmp[num_items].waitfor = ZX_EVENTPAIR_SIGNALED;
  tmp[num_items].handle = event.get();
  zx_status_t status = _zx_object_wait_many(tmp, num_items + 1, ZX_TIME_INFINITE);
  // copy everything back:
  std::copy_n(tmp, num_items, items);
  if (status != ZX_OK) {
    return status;
  }
  if ((tmp[num_items].pending & ZX_EVENTPAIR_SIGNALED) != 0) {
    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

// port_wait adds an extra fake-clock eventpair handle to the port and changes the deadline to
// ZX_TIME_INFINITE.
__EXPORT zx_status_t zx_port_wait(zx_handle_t handle, zx_time_t deadline,
                                  zx_port_packet_t* packet) {
  if (deadline == ZX_TIME_INFINITE) {
    return _zx_port_wait(handle, deadline, packet);
  }

  zx::eventpair event = MakeEvent(deadline);
  uint64_t key = 0xFACEFACE00000000 | event.get();
  if (zx_status_t status = zx_object_wait_async(event.get(), handle, key, ZX_EVENTPAIR_SIGNALED, 0);
      status != ZX_OK) {
    ZX_PANIC("%s", zx_status_get_string(status));
  }
  zx_port_packet_t tmp;
  zx_status_t status = _zx_port_wait(handle, ZX_TIME_INFINITE, &tmp);
  // always cancel the wait in case it wasn't a timeout
  zx::unowned_port(handle)->cancel(event, key);
  if (status != ZX_OK) {
    return status;
  }
  if (tmp.type == ZX_PKT_TYPE_SIGNAL_ONE && tmp.key == key &&
      tmp.signal.observed == ZX_EVENTPAIR_SIGNALED) {
    return ZX_ERR_TIMED_OUT;
  }
  *packet = tmp;
  return ZX_OK;
}

// timer_create changes the type of returned handle from an actual timer to one side of an eventpair
// created by fake-clock. It relies on the fact that ZX_EVENTPAIR_SIGNALED is the same bit as
// ZX_TIMER_SIGNALED, meaning unless clients are inspecting the handle type, they shouldn't be able
// to tell the difference.
__EXPORT zx_status_t zx_timer_create(uint32_t options, zx_clock_t clock_id, zx_handle_t* out) {
  // We're replacing a timer with an event, and shamelessly using the fact that
  // ZX_EVENTPAIR_SIGNALED and ZX_TIMER_SIGNAL collide, this assertion protects that assumption more
  // strongly.
  static_assert(ZX_EVENTPAIR_SIGNALED == ZX_TIMER_SIGNALED);
  if (clock_id != ZX_CLOCK_MONOTONIC) {
    // NOTE: _zx_timer_create will just fail according to the docs.
    return _zx_timer_create(options, clock_id, out);
  }
  // Create an event with infinite deadline and return that instead of a timer handle
  *out = MakeEvent(ZX_TIME_INFINITE).release();
  return ZX_OK;
}

__EXPORT zx_status_t zx_timer_set(zx_handle_t handle, zx_time_t deadline, zx_duration_t slack) {
  zx::eventpair e;
  if (zx_status_t status = zx::unowned_eventpair(handle)->duplicate(ZX_RIGHT_SAME_RIGHTS, &e);
      status != ZX_OK) {
    return status;
  }
  // reschedule the event with the fake clock service:
  const fidl::WireResult result =
      fidl::WireCall(GetService())->RescheduleEvent(std::move(e), deadline);
  ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  return ZX_OK;
}

__EXPORT zx_status_t zx_timer_cancel(zx_handle_t handle) {
  zx::eventpair e;
  if (zx_status_t status = zx::unowned_eventpair(handle)->duplicate(ZX_RIGHT_SAME_RIGHTS, &e);
      status != ZX_OK) {
    return status;
  }
  const fidl::WireResult result = fidl::WireCall(GetService())->CancelEvent(std::move(e));
  ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  return ZX_OK;
}

__EXPORT bool create_named_deadline(char* component, size_t component_len, char* code,
                                    size_t code_len, zx_time_t duration, zx_time_t* out) {
  const fidl::WireResult result =
      fidl::WireCall(GetService())
          ->CreateNamedDeadline(
              {
                  .component_id = fidl::StringView::FromExternal(component, component_len),
                  .code = fidl::StringView::FromExternal(code, code_len),
              },
              duration);
  ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  *out = result.value().deadline;
  return true;
}
