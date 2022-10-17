// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/arena.h>
#include <lib/fdf/channel.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/env.h>
#include <lib/fdf/testing.h>
#include <lib/fdf/token.h>
#include <lib/fit/defer.h>

#include "src/devices/bin/driver_runtime/arena.h"
#include "src/devices/bin/driver_runtime/channel.h"
#include "src/devices/bin/driver_runtime/dispatcher.h"
#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/bin/driver_runtime/handle.h"

// fdf_arena_t interface

__EXPORT zx_status_t fdf_arena_create(uint32_t options, uint32_t tag, fdf_arena_t** out_arena) {
  return fdf_arena::Create(options, tag, out_arena);
}

__EXPORT void* fdf_arena_allocate(fdf_arena_t* arena, size_t bytes) {
  return arena->Allocate(bytes);
}

__EXPORT void fdf_arena_free(fdf_arena_t* arena, void* data) { return arena->Free(data); }

__EXPORT bool fdf_arena_contains(fdf_arena_t* arena, const void* data, size_t num_bytes) {
  return arena->Contains(data, num_bytes);
}

__EXPORT void fdf_arena_destroy(fdf_arena_t* arena) { arena->Destroy(); }

// fdf_channel_t interface

__EXPORT
zx_status_t fdf_channel_create(uint32_t options, fdf_handle_t* out0, fdf_handle_t* out1) {
  if (!out0 || !out1) {
    return ZX_ERR_INVALID_ARGS;
  }
  return driver_runtime::Channel::Create(options, out0, out1);
}

__EXPORT
zx_status_t fdf_channel_write(fdf_handle_t channel_handle, uint32_t options, fdf_arena_t* arena,
                              void* data, uint32_t num_bytes, zx_handle_t* handles,
                              uint32_t num_handles) {
  fbl::RefPtr<driver_runtime::Channel> channel;
  zx_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_handle, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->Write(options, arena, data, num_bytes, handles, num_handles);
}

__EXPORT
zx_status_t fdf_channel_read(fdf_handle_t channel_handle, uint32_t options, fdf_arena_t** arena,
                             void** data, uint32_t* num_bytes, zx_handle_t** handles,
                             uint32_t* num_handles) {
  fbl::RefPtr<driver_runtime::Channel> channel;
  zx_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_handle, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->Read(options, arena, data, num_bytes, handles, num_handles);
}

__EXPORT
zx_status_t fdf_channel_wait_async(struct fdf_dispatcher* dispatcher,
                                   fdf_channel_read_t* channel_read, uint32_t options) {
  if (!channel_read) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::RefPtr<driver_runtime::Channel> channel;
  zx_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_read->channel, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->WaitAsync(dispatcher, channel_read, options);
}

__EXPORT zx_status_t fdf_channel_call(fdf_handle_t channel_handle, uint32_t options,
                                      zx_time_t deadline, const fdf_channel_call_args_t* args) {
  fbl::RefPtr<driver_runtime::Channel> channel;
  zx_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_handle, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->Call(options, deadline, args);
}

__EXPORT zx_status_t fdf_channel_cancel_wait(fdf_handle_t channel_handle) {
  fbl::RefPtr<driver_runtime::Channel> channel;
  zx_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_handle, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->CancelWait();
}

__EXPORT void fdf_handle_close(fdf_handle_t channel_handle) {
  if (channel_handle == FDF_HANDLE_INVALID) {
    return;
  }
  if (!driver_runtime::Handle::IsFdfHandle(channel_handle)) {
    zx_handle_close(channel_handle);
    return;
  }
  driver_runtime::Handle* handle = driver_runtime::Handle::MapValueToHandle(channel_handle);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(handle);

  fbl::RefPtr<driver_runtime::Channel> channel;
  zx_status_t status = handle->GetObject<driver_runtime::Channel>(&channel);
  if (status != ZX_OK) {
    return;
  }
  channel->Close();
  // Drop the handle.
  handle->TakeOwnership();
}

// fdf_dispatcher_t interface
__EXPORT zx_status_t fdf_dispatcher_create(uint32_t options, const char* name, size_t name_len,
                                           const char* scheduler_role, size_t scheduler_role_len,
                                           fdf_dispatcher_shutdown_observer_t* observer,
                                           fdf_dispatcher_t** out_dispatcher) {
  driver_runtime::Dispatcher* dispatcher;
  auto status = driver_runtime::Dispatcher::Create(
      options, std::string_view(name, name_len),
      std::string_view(scheduler_role, scheduler_role_len), observer, &dispatcher);
  if (status != ZX_OK) {
    return status;
  }
  *out_dispatcher = static_cast<fdf_dispatcher*>(dispatcher);
  return ZX_OK;
}

__EXPORT async_dispatcher_t* fdf_dispatcher_get_async_dispatcher(fdf_dispatcher_t* dispatcher) {
  return dispatcher->GetAsyncDispatcher();
}

__EXPORT fdf_dispatcher_t* fdf_dispatcher_from_async_dispatcher(async_dispatcher_t* dispatcher) {
  return static_cast<fdf_dispatcher*>(fdf_dispatcher::FromAsyncDispatcher(dispatcher));
}

__EXPORT uint32_t fdf_dispatcher_get_options(const fdf_dispatcher_t* dispatcher) {
  return dispatcher->options();
}

__EXPORT void fdf_dispatcher_shutdown_async(fdf_dispatcher_t* dispatcher) {
  return dispatcher->ShutdownAsync();
}

__EXPORT void fdf_dispatcher_destroy(fdf_dispatcher_t* dispatcher) { return dispatcher->Destroy(); }

__EXPORT fdf_dispatcher_t* fdf_dispatcher_get_current_dispatcher() {
  return static_cast<fdf_dispatcher_t*>(driver_context::GetCurrentDispatcher());
}

__EXPORT zx_status_t fdf_token_register(zx_handle_t token, fdf_dispatcher_t* dispatcher,
                                        fdf_token_t* handler) {
  return driver_runtime::DispatcherCoordinator::TokenRegister(token, dispatcher, handler);
}

__EXPORT zx_status_t fdf_token_exchange(zx_handle_t token, fdf_handle_t handle) {
  return driver_runtime::DispatcherCoordinator::TokenExchange(token, handle);
}

__EXPORT void fdf_env_register_driver_entry(const void* driver) {
  driver_context::PushDriver(driver);
}

__EXPORT void fdf_env_register_driver_exit() { driver_context::PopDriver(); }

__EXPORT zx_status_t fdf_env_dispatcher_create_with_owner(
    const void* driver, uint32_t options, const char* name, size_t name_len,
    const char* scheduler_role, size_t scheduler_role_len,
    fdf_dispatcher_shutdown_observer_t* observer, fdf_dispatcher_t** out_dispatcher) {
  driver_context::PushDriver(driver);
  auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

  driver_runtime::Dispatcher* dispatcher;
  auto status = driver_runtime::Dispatcher::Create(
      options, std::string_view(name, name_len),
      std::string_view(scheduler_role, scheduler_role_len), observer, &dispatcher);
  if (status != ZX_OK) {
    return status;
  }
  *out_dispatcher = static_cast<fdf_dispatcher*>(dispatcher);
  return ZX_OK;
}

__EXPORT const void* fdf_env_get_current_driver() { return driver_context::GetCurrentDriver(); }

__EXPORT zx_status_t fdf_env_shutdown_dispatchers_async(
    const void* driver, fdf_env_driver_shutdown_observer_t* observer) {
  return driver_runtime::DispatcherCoordinator::ShutdownDispatchersAsync(driver, observer);
}

__EXPORT void fdf_env_destroy_all_dispatchers() {
  return driver_runtime::DispatcherCoordinator::DestroyAllDispatchers();
}

__EXPORT bool fdf_env_dispatcher_has_queued_tasks(fdf_dispatcher_t* dispatcher) {
  return dispatcher->HasQueuedTasks();
}

__EXPORT void fdf_testing_push_driver(const void* driver) { driver_context::PushDriver(driver); }

__EXPORT void fdf_testing_pop_driver() { driver_context::PopDriver(); }

__EXPORT void fdf_testing_wait_until_all_dispatchers_idle() {
  return driver_runtime::DispatcherCoordinator::WaitUntilDispatchersIdle();
}

__EXPORT void fdf_testing_wait_until_all_dispatchers_destroyed() {
  return driver_runtime::DispatcherCoordinator::WaitUntilDispatchersDestroyed();
}
