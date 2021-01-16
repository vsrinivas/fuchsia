// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_SYNC_SYNC_COMMON_DEFS_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_SYNC_SYNC_COMMON_DEFS_H_

#include <cstdint>

namespace goldfish {

namespace sync {

// The definitions below (command codes, register layout)
// need to be in sync with the following files:
//
// Host-side (AOSP platform/external/qemu repository):
// - android/emulation/goldfish_sync.h
// - hw/misc/goldfish_sync.c
//
// Guest-side (AOSP device/generic/goldfish-opengl repository):
// - system/egl/goldfish_sync.h
//
enum SyncRegs {
  // host->guest batch commands
  SYNC_REG_BATCH_COMMAND = 0x00,

  // guest->host batch commands
  SYNC_REG_BATCH_GUESTCOMMAND = 0x04,

  // communicate physical address of host->guest batch commands
  SYNC_REG_BATCH_COMMAND_ADDR = 0x08,
  SYNC_REG_BATCH_COMMAND_ADDR_HIGH = 0x0C, /* 64-bit part */

  // communicate physical address of guest->host commands
  SYNC_REG_BATCH_GUESTCOMMAND_ADDR = 0x10,
  SYNC_REG_BATCH_GUESTCOMMAND_ADDR_HIGH = 0x14, /* 64-bit part */

  // signals that the device has been probed
  SYNC_REG_INIT = 0x18,
};

enum CommandId {
  // Ready signal - used to mark when irq should lower
  CMD_SYNC_READY = 0,

  // Create a new timeline. writes timeline handle
  CMD_CREATE_SYNC_TIMELINE = 1,

  // Create a fence object. reads timeline handle and time argument.
  // Writes fence fd to the SYNC_REG_HANDLE register.
  CMD_CREATE_SYNC_FENCE = 2,

  // Increments timeline. reads timeline handle and time argument
  CMD_SYNC_TIMELINE_INC = 3,

  // Destroys a timeline. reads timeline handle
  CMD_DESTROY_SYNC_TIMELINE = 4,

  // Starts a wait on the host with the given glsync object and
  // sync thread handle.
  CMD_TRIGGER_HOST_WAIT = 5,
};

struct HostCommand {
  uint64_t handle;
  uint64_t hostcmd_handle;
  uint32_t cmd;
  uint32_t time_arg;
};

struct GuestCommand {
  uint64_t host_command;
  uint64_t glsync_handle;
  uint64_t thread_handle;
  uint64_t guest_timeline_handle;
};

// Device-level set of buffers shared with the host.
struct CommandBuffers {
  HostCommand batch_hostcmd;
  GuestCommand batch_guestcmd;
};

}  // namespace sync

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_SYNC_SYNC_COMMON_DEFS_H_
