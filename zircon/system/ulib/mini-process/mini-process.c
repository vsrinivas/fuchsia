// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <lib/elf-psabi/sp.h>
#include <stdint.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <elfload/elfload.h>
#include <mini-process/mini-process.h>

#include "subprocess.h"

static void* get_syscall_addr(const void* syscall_fn, uintptr_t vdso_base) {
  Dl_info dl_info;
  if (dladdr(syscall_fn, &dl_info) == 0)
    return 0;
  return (void*)(vdso_base + ((uintptr_t)dl_info.dli_saddr - (uintptr_t)dl_info.dli_fbase));
}

static zx_status_t write_ctx_message(zx_handle_t channel, uintptr_t vdso_base,
                                     zx_handle_t transferred_handle) {
  minip_ctx_t ctx = {
      .handle_close = get_syscall_addr(&zx_handle_close, vdso_base),
      .object_wait_async = get_syscall_addr(&zx_object_wait_async, vdso_base),
      .object_wait_one = get_syscall_addr(&zx_object_wait_one, vdso_base),
      .object_signal = get_syscall_addr(&zx_object_signal, vdso_base),
      .event_create = get_syscall_addr(&zx_event_create, vdso_base),
      .profile_create = get_syscall_addr(&zx_profile_create, vdso_base),
      .channel_create = get_syscall_addr(&zx_channel_create, vdso_base),
      .channel_read = get_syscall_addr(&zx_channel_read, vdso_base),
      .channel_write = get_syscall_addr(&zx_channel_write, vdso_base),
      .process_exit = get_syscall_addr(&zx_process_exit, vdso_base),
      .object_get_info = get_syscall_addr(&zx_object_get_info, vdso_base),
      .port_cancel = get_syscall_addr(&zx_port_cancel, vdso_base),
      .port_create = get_syscall_addr(&zx_port_create, vdso_base),
      .pager_create = get_syscall_addr(&zx_pager_create, vdso_base),
      .pager_create_vmo = get_syscall_addr(&zx_pager_create_vmo, vdso_base),
      .vmo_contiguous_create = get_syscall_addr(&zx_vmo_create_contiguous, vdso_base),
      .vmo_physical_create = get_syscall_addr(&zx_vmo_create_physical, vdso_base),
      .vmo_replace_as_executable = get_syscall_addr(&zx_vmo_replace_as_executable, vdso_base),
  };
  uint32_t actual_handles = (transferred_handle == ZX_HANDLE_INVALID) ? 0u : 1u;
  return zx_channel_write(channel, 0u, &ctx, sizeof(ctx), &transferred_handle, actual_handles);
}

// Sets up a VMO on the given VMAR which contains the mini process code and space for a stack.
__EXPORT
zx_status_t mini_process_load_stack(zx_handle_t vmar, bool with_code, zx_vaddr_t* stack_base,
                                    zx_vaddr_t* sp) {
  // Allocate a single VMO for the child. It doubles as the stack on the top and
  // as the executable code (minipr_thread_loop()) at the bottom. In theory, actual
  // stack usage is minimal, like 160 bytes or less.
  uint64_t stack_size = 16 * 1024u;
  zx_handle_t stack_vmo = ZX_HANDLE_INVALID;
  zx_status_t status = zx_vmo_create(stack_size, 0, &stack_vmo);
  if (status != ZX_OK)
    return status;
  // Try to set the name, but ignore any errors since it's purely for
  // debugging and diagnostics.
  static const char vmo_name[] = "mini-process:stack";
  zx_object_set_property(stack_vmo, ZX_PROP_NAME, vmo_name, sizeof(vmo_name));

  zx_vm_option_t perms = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  if (with_code) {
    // We assume that the code to execute is less than kSizeLimit bytes.
    const uint32_t kSizeLimit = 2000;
    status = zx_vmo_write(stack_vmo, &minipr_thread_loop, 0u, kSizeLimit);
    if (status != ZX_OK)
      goto exit;

    // TODO(mdempsky): Separate minipr_thread_loop and stack into
    // separate VMOs to enforce W^X.
    status = zx_vmo_replace_as_executable(stack_vmo, ZX_HANDLE_INVALID, &stack_vmo);
    if (status != ZX_OK)
      goto exit;

    perms |= ZX_VM_PERM_EXECUTE;
  }

  status = zx_vmar_map(vmar, perms, 0, stack_vmo, 0, stack_size, stack_base);
  if (status != ZX_OK)
    goto exit;

  // Compute a valid starting SP for the machine's ABI.
  *sp = compute_initial_stack_pointer(*stack_base, stack_size);

exit:
  // Close the VMO handle no matter what; if we failed we want to release it, and if
  // we succeeded the VMAR retains a reference to it so we don't need a handle.
  zx_handle_close(stack_vmo);
  return status;
}

__EXPORT
zx_status_t mini_process_load_vdso(zx_handle_t process, zx_handle_t vmar, uintptr_t* base,
                                   uintptr_t* entry) {
  // This is not thread-safe.  It steals the startup handle, so it's not
  // compatible with also using launchpad (which also needs to steal the
  // startup handle).
  static zx_handle_t vdso_vmo = ZX_HANDLE_INVALID;
  if (vdso_vmo == ZX_HANDLE_INVALID) {
    vdso_vmo = zx_take_startup_handle(PA_HND(PA_VMO_VDSO, 0));
    if (vdso_vmo == ZX_HANDLE_INVALID) {
      return ZX_ERR_INTERNAL;
    }
  }

  elf_load_header_t header;
  uintptr_t phoff;
  zx_status_t status = elf_load_prepare(vdso_vmo, NULL, 0, &header, &phoff);
  if (status == ZX_OK) {
    elf_phdr_t phdrs[header.e_phnum];
    status = elf_load_read_phdrs(vdso_vmo, phdrs, phoff, header.e_phnum);
    if (status == ZX_OK) {
      status = elf_load_map_segments(vmar, &header, phdrs, vdso_vmo, NULL, base, entry);
    }
  }

  return status;
}

__EXPORT
zx_status_t mini_process_wait_for_ack(zx_handle_t control_channel) {
  zx_signals_t observed;
  zx_status_t status = zx_object_wait_one(
      control_channel, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, &observed);
  if (status != ZX_OK) {
    return status;
  }

  if (observed & ZX_CHANNEL_PEER_CLOSED) {
    // the child process died prematurely.
    return ZX_ERR_UNAVAILABLE;
  }

  if (observed & ZX_CHANNEL_READABLE) {
    uint32_t ack[2];
    uint32_t actual_handles;
    uint32_t actual_bytes;
    status = zx_channel_read(control_channel, 0u, ack, NULL, sizeof(uint32_t) * 2, 0u,
                             &actual_bytes, &actual_handles);
  } else {
    status = ZX_ERR_BAD_STATE;
  }

  return status;
}

__EXPORT
zx_status_t start_mini_process_etc(zx_handle_t process, zx_handle_t thread, zx_handle_t vmar,
                                   zx_handle_t transferred_handle, bool wait_for_ack,
                                   zx_handle_t* control_channel) {
  zx_vaddr_t stack_base = 0;
  zx_vaddr_t sp = 0;
  zx_status_t status = mini_process_load_stack(vmar, true, &stack_base, &sp);
  if (status != ZX_OK)
    return status;

  zx_handle_t chn[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};

  if (!control_channel) {
    // Simple mode /////////////////////////////////////////////////////////////
    // Don't map the VDSO, so the only thing the mini-process can do is busy-loop.
    // The handle sent to the process is just the caller's handle.
    status = zx_process_start(process, thread, stack_base, sp, transferred_handle, 0);
    transferred_handle = ZX_HANDLE_INVALID;

  } else {
    // Complex mode ////////////////////////////////////////////////////////////
    // The mini-process is going to run a simple request-response over a channel
    // So we need to:
    // 1- map the VDSO in the child process, without launchpad.
    // 2- create a channel and give one end to the child process.
    // 3- send a message with the rest of the syscall function addresses.
    // 4- wait for reply.

    status = zx_channel_create(0u, &chn[0], &chn[1]);
    if (status != ZX_OK)
      goto exit;

    uintptr_t vdso_base = 0;
    status = mini_process_load_vdso(process, vmar, &vdso_base, NULL);
    if (status != ZX_OK)
      goto exit;

    status = write_ctx_message(chn[0], vdso_base, transferred_handle);
    transferred_handle = ZX_HANDLE_INVALID;
    if (status != ZX_OK)
      goto exit;

    uintptr_t channel_read = (uintptr_t)get_syscall_addr(&zx_channel_read, vdso_base);

    status = zx_process_start(process, thread, stack_base, sp, chn[1], channel_read);
    chn[1] = ZX_HANDLE_INVALID;
    if (status != ZX_OK)
      goto exit;

    if (wait_for_ack) {
      status = mini_process_wait_for_ack(chn[0]);
      if (status != ZX_OK) {
        goto exit;
      }
    }

    *control_channel = chn[0];
    chn[0] = ZX_HANDLE_INVALID;
  }

exit:
  if (transferred_handle != ZX_HANDLE_INVALID)
    zx_handle_close(transferred_handle);
  if (chn[0] != ZX_HANDLE_INVALID)
    zx_handle_close(chn[0]);
  if (chn[1] != ZX_HANDLE_INVALID)
    zx_handle_close(chn[1]);

  return status;
}

__EXPORT
zx_status_t mini_process_cmd_send(zx_handle_t cntrl_channel, uint32_t what) {
  minip_cmd_t cmd = {.what = what, .status = ZX_OK};

  return zx_channel_write(cntrl_channel, 0, &cmd, sizeof(cmd), NULL, 0);
}

__EXPORT
zx_status_t mini_process_cmd_read_reply(zx_handle_t cntrl_channel, zx_handle_t* handle) {
  zx_status_t status = zx_object_wait_one(
      cntrl_channel, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, NULL);
  if (status != ZX_OK)
    return status;
  minip_cmd_t reply;
  uint32_t handle_count = handle ? 1 : 0;
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  status = zx_channel_read(cntrl_channel, 0, &reply, handle, sizeof(reply), handle_count,
                           &actual_bytes, &actual_handles);
  if (status != ZX_OK)
    return status;
  return reply.status;
}

__EXPORT
zx_status_t mini_process_cmd(zx_handle_t cntrl_channel, uint32_t what, zx_handle_t* handle) {
  zx_status_t status = mini_process_cmd_send(cntrl_channel, what);
  if (status != ZX_OK)
    return status;
  return mini_process_cmd_read_reply(cntrl_channel, handle);
}

__EXPORT
zx_status_t start_mini_process(zx_handle_t job, zx_handle_t transferred_handle,
                               zx_handle_t* process, zx_handle_t* thread) {
  *process = ZX_HANDLE_INVALID;
  zx_handle_t vmar = ZX_HANDLE_INVALID;
  zx_handle_t channel = ZX_HANDLE_INVALID;

  zx_status_t status = zx_process_create(job, "minipr", 6u, 0u, process, &vmar);
  if (status != ZX_OK)
    goto exit;

  *thread = ZX_HANDLE_INVALID;
  status = zx_thread_create(*process, "minith", 6u, 0, thread);
  if (status != ZX_OK)
    goto exit;

  status = start_mini_process_etc(*process, *thread, vmar, transferred_handle, true, &channel);
  transferred_handle = ZX_HANDLE_INVALID;  // The transferred_handle gets consumed.
exit:
  if (status != ZX_OK) {
    if (transferred_handle != ZX_HANDLE_INVALID)
      zx_handle_close(transferred_handle);
    if (*process != ZX_HANDLE_INVALID)
      zx_handle_close(*process);
    if (*thread != ZX_HANDLE_INVALID)
      zx_handle_close(*thread);
  }

  if (channel != ZX_HANDLE_INVALID)
    zx_handle_close(channel);

  return status;
}

__EXPORT
zx_status_t start_mini_process_thread(zx_handle_t thread, zx_handle_t vmar) {
  zx_vaddr_t stack_base = 0;
  zx_vaddr_t sp = 0;
  zx_status_t status = mini_process_load_stack(vmar, true, &stack_base, &sp);
  if (status != ZX_OK)
    return status;

  return zx_thread_start(thread, stack_base, sp, 0, 0);
}
