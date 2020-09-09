// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "subprocess.h"

#include <lib/backtrace-request/backtrace-request.h>

#include <mini-process/mini-process.h>

// This file assumes that it's compiled with minimal optimization to avoid
// things like the compiler splitting the code into hot and code sections that
// are no longer contiguous.  The use of minipr_thread_loop assumes that the
// function is one self-contained contiguous chunk of code.  So it cannot be
// split up, and it cannot make any direct calls.
//
// This function is the entire program that the child process will execute. It
// gets directly mapped into the child process via zx_vmo_write() so it,
//
// 1. must not reference any addressable entity outside the function
//
// 2. must fit entirely within its containing VMO
//
// If you find that this program is crashing for no apparent reason, check to
// see if it has outgrown its VMO. See kSizeLimit in mini-process.c.

// Specify an explicit section for the function defeats hot/cold section
// splitting optimizations.
__attribute__((section(".text.not-split"))) void minipr_thread_loop(zx_handle_t channel,
                                                                    uintptr_t fnptr) {
  if (fnptr == 0) {
    // In this mode we don't have a VDSO so we don't care what the handle is
    // and therefore we busy-loop. Unless external steps are taken this will
    // saturate one core.
    volatile uint32_t val = 1;
    while (val) {
      val += 2u;
    }
  } else {
    // In this mode we do have a VDSO but we are not a real ELF program so
    // we need to receive from the parent the address of the syscalls we can
    // use. So we can bootstrap, kernel has already transferred the address of
    // zx_channel_read() and the handle to one end of the channel which already
    // contains a message with the rest of the syscall addresses.
    __typeof(zx_channel_read)* read_fn = (__typeof(zx_channel_read)*)fnptr;

    uint32_t actual = 0u;
    uint32_t actual_handles = 0u;
    zx_handle_t original_handle = ZX_HANDLE_INVALID;
    minip_ctx_t ctx = {0};

    zx_status_t status =
        (*read_fn)(channel, 0u, &ctx, &original_handle, sizeof(ctx), 1, &actual, &actual_handles);
    if ((status != ZX_OK) || (actual != sizeof(ctx)))
      __builtin_trap();

    // The received handle in the |ctx| message does not have any use other than
    // keeping it alive until the process ends. We basically leak it.

    // Acknowledge the initial message.
    uint32_t ack[2] = {actual, actual_handles};
    status = ctx.channel_write(channel, 0u, ack, sizeof(uint32_t) * 2, NULL, 0u);
    if (status != ZX_OK)
      __builtin_trap();

    do {
      // wait for the next message.
      status = ctx.object_wait_one(channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, &actual);
      if (status != ZX_OK)
        break;

      minip_cmd_t cmd = {0};
      status = ctx.channel_read(channel, 0u, &cmd, NULL, sizeof(cmd), 0u, &actual, &actual_handles);

      if (status != ZX_OK)
        break;

      // Execute one or more commands. After each we send a reply with the
      // result. If the command does not cause to crash or exit.
      uint32_t what = cmd.what;

      do {
        // This loop is convoluted. A simpler switch() statement
        // has the risk of being generated as a table lookup which
        // makes it likely it will reference the data section which
        // is outside the memory copied to the child.

        zx_handle_t handle[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};

        if (what & MINIP_CMD_ECHO_MSG) {
          what &= ~MINIP_CMD_ECHO_MSG;
          cmd.status = ZX_OK;
          goto reply;
        }
        if (what & MINIP_CMD_CREATE_EVENT) {
          what &= ~MINIP_CMD_CREATE_EVENT;
          cmd.status = ctx.event_create(0u, &handle[0]);
          goto reply;
        }
        if (what & MINIP_CMD_CREATE_PROFILE) {
          what &= ~MINIP_CMD_CREATE_PROFILE;

          // zx_profile_create() needs a handle to the root job, but we don't have one so
          // we're passing ZX_HANDLE_INVALID. It is expected that this call will fail.
          //
          // Note, we're passing NULL instead of a pointer to a properly initialized
          // zx_profile_info_t. That's to prevent the compiler from getting smart and
          // using a pre-computed structure in the data segment. This function is
          // "injected" into the mini-process so there can be no external dependencies.
          cmd.status = ctx.profile_create(ZX_HANDLE_INVALID, 0u, NULL, &handle[0]);
          goto reply;
        }
        if (what & MINIP_CMD_CREATE_CHANNEL) {
          what &= ~MINIP_CMD_CREATE_CHANNEL;
          cmd.status = ctx.channel_create(0u, &handle[0], &handle[1]);
          goto reply;
        }
        if (what & MINIP_CMD_USE_BAD_HANDLE_CLOSED) {
          what &= ~MINIP_CMD_USE_BAD_HANDLE_CLOSED;

          // Test one case of using an invalid handle.  This
          // tests a double-close of an event handle.
          zx_handle_t handle = ZX_HANDLE_INVALID;
          if (ctx.event_create(0u, &handle) != ZX_OK || ctx.handle_close(handle) != ZX_OK)
            __builtin_trap();
          cmd.status = ctx.handle_close(handle);
          goto reply;
        }
        if (what & MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED) {
          what &= ~MINIP_CMD_USE_BAD_HANDLE_TRANSFERRED;

          // Test another case of using an invalid handle.  This
          // tests closing a handle after it has been transferred
          // out of the process (by writing it to a channel).  In
          // this case, the Handle object still exists inside the
          // kernel.
          zx_handle_t handle = ZX_HANDLE_INVALID;
          zx_handle_t channel1 = ZX_HANDLE_INVALID;
          zx_handle_t channel2 = ZX_HANDLE_INVALID;
          if (ctx.event_create(0u, &handle) != ZX_OK ||
              ctx.channel_create(0u, &channel1, &channel2) != ZX_OK ||
              ctx.channel_write(channel1, 0, NULL, 0, &handle, 1) != ZX_OK)
            __builtin_trap();
          // This should produce an error and/or exception.
          cmd.status = ctx.handle_close(handle);
          // Clean up.
          if (ctx.handle_close(channel1) != ZX_OK || ctx.handle_close(channel2) != ZX_OK)
            __builtin_trap();
          goto reply;
        }
        if (what & MINIP_CMD_VALIDATE_CLOSED_HANDLE) {
          what &= ~MINIP_CMD_VALIDATE_CLOSED_HANDLE;

          zx_handle_t event = ZX_HANDLE_INVALID;
          if (ctx.event_create(0u, &event) != ZX_OK)
            __builtin_trap();
          ctx.handle_close(event);
          cmd.status = ctx.object_get_info(event, ZX_INFO_HANDLE_VALID, NULL, 0, NULL, NULL);
          goto reply;
        }
        if (what & MINIP_CMD_CREATE_PAGER_VMO) {
          what &= ~MINIP_CMD_CREATE_PAGER_VMO;

          zx_handle_t pager = ZX_HANDLE_INVALID;
          if (ctx.pager_create(0u, &pager) != ZX_OK)
            __builtin_trap();
          zx_handle_t port = ZX_HANDLE_INVALID;
          if (ctx.port_create(0u, &port) != ZX_OK)
            __builtin_trap();
          cmd.status = ctx.pager_create_vmo(pager, 0u, port, 0u, 0u, &handle[0]);
          goto reply;
        }
        if (what & MINIP_CMD_CREATE_VMO_CONTIGUOUS) {
          what &= ~MINIP_CMD_CREATE_VMO_CONTIGUOUS;

          // This call will fail because we don't have a bti handle, but that's OK because
          // we only care about *how* it fails.
          cmd.status = ctx.vmo_contiguous_create(ZX_HANDLE_INVALID, ZX_PAGE_SIZE, 0u, &handle[0]);
          goto reply;
        }
        if (what & MINIP_CMD_CREATE_VMO_PHYSICAL) {
          what &= ~MINIP_CMD_CREATE_VMO_PHYSICAL;

          // This call will fail because we don't have a mmio resource, but that's OK
          // because we only care about *how* it fails.
          cmd.status = ctx.vmo_physical_create(ZX_HANDLE_INVALID, 0u, 0u, &handle[0]);
          goto reply;
        }
        if (what & MINIP_CMD_CHANNEL_WRITE) {
          what &= ~MINIP_CMD_CHANNEL_WRITE;

          uint8_t val = 0;
          cmd.status = ctx.channel_write(original_handle, 0, &val, 1, NULL, 0u);
          goto reply;
        }
        if (what & MINIP_CMD_BACKTRACE_REQUEST) {
          what &= ~MINIP_CMD_BACKTRACE_REQUEST;

          backtrace_request();
          cmd.status = ZX_OK;
          goto reply;
        }
        if (what & MINIP_CMD_ATTEMPT_AMBIENT_EXECUTABLE) {
          what &= ~MINIP_CMD_ATTEMPT_AMBIENT_EXECUTABLE;
          zx_handle_t vmo = ZX_HANDLE_INVALID;
          zx_handle_t pager = ZX_HANDLE_INVALID;
          zx_handle_t port = ZX_HANDLE_INVALID;

          // We use builtin_trap to kill off the process in a way
          // that distinguishes a failure in these calls from an
          // intended failure.
          if (ctx.pager_create(0u, &pager) != ZX_OK)
            __builtin_trap();
          if (ctx.port_create(0u, &port) != ZX_OK)
            __builtin_trap();
          if (ctx.pager_create_vmo(pager, 0u, port, 0u, 0u, &vmo) != ZX_OK)
            __builtin_trap();

          cmd.status = ctx.vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &vmo);
          goto reply;
        }
        if (what & MINIP_CMD_CHECK_THREAD_POINTER) {
          what &= ~MINIP_CMD_CHECK_THREAD_POINTER;
          uintptr_t value;
#ifdef __x86_64__
          __asm__ volatile("mov %%fs:0, %0" : "=r"(value));
#else
          value = *(uintptr_t*)__builtin_thread_pointer();
#endif
          if (value == MINIP_THREAD_POINTER_CHECK_VALUE) {
            cmd.status = ZX_OK;
          } else {
            cmd.status = ZX_ERR_BAD_STATE;
          }
          goto reply;
        }
        if (what & MINIP_CMD_WAIT_ASYNC_CANCEL) {
          zx_handle_t port = ZX_HANDLE_INVALID;
          if (ctx.port_create(0u, &port) != ZX_OK) {
            __builtin_trap();
          }
          while (1) {
            if (ctx.object_wait_async(original_handle, port, 42, ZX_USER_SIGNAL_0, 0) != ZX_OK) {
              __builtin_trap();
            }
            if (ctx.port_cancel(port, original_handle, 42) != ZX_OK) {
              __builtin_trap();
            }
          }
          cmd.status = ZX_OK;
          goto reply;
        }

        // The following don't send a message so the client will get ZX_CHANNEL_PEER_CLOSED.

        if (what & MINIP_CMD_BUILTIN_TRAP)
          __builtin_trap();

        if (what & MINIP_CMD_EXIT_NORMAL)
          ctx.process_exit(0);

        if (what & MINIP_CMD_THREAD_EXIT)
          ctx.thread_exit();

        // Did not match any known message.
        cmd.status = ZX_ERR_WRONG_TYPE;
      reply:
        actual_handles = (handle[0] == ZX_HANDLE_INVALID) ? 0u : 1u;
        status = ctx.channel_write(channel, 0u, &cmd, sizeof(cmd), handle, actual_handles);

        // Loop if there are more commands packed in |what|.
      } while (what);

    } while (status == ZX_OK);
  }

  __builtin_trap();
}
