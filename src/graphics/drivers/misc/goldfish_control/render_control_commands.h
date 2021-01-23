// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_RENDER_CONTROL_COMMANDS_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_RENDER_CONTROL_COMMANDS_H_

#include <cstdint>
#include <memory>

namespace goldfish {

struct CreateColorBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint32_t internalformat;
};
constexpr uint32_t kOP_rcCreateColorBuffer = 10012;
constexpr uint32_t kSize_rcCreateColorBuffer = 20;

struct CloseColorBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
};
constexpr uint32_t kOP_rcCloseColorBuffer = 10014;
constexpr uint32_t kSize_rcCloseColorBuffer = 12;

struct CloseBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
};
constexpr uint32_t kOP_rcCloseBuffer = 10050;
constexpr uint32_t kSize_rcCloseBuffer = 12;

struct SetColorBufferVulkanMode2Cmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
  uint32_t mode;
  uint32_t memory_property;
};
constexpr uint32_t kOP_rcSetColorBufferVulkanMode2 = 10051;
constexpr uint32_t kSize_rcSetColorBufferVulkanMode2 = 20;

struct __attribute__((__packed__)) MapGpaToBufferHandle2Cmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
  uint64_t gpa;
  uint64_t map_size;
};
constexpr uint32_t kOP_rcMapGpaToBufferHandle2 = 10054;
constexpr uint32_t kSize_rcMapGpaToBufferHandle2 = 28;

struct __attribute__((__packed__)) CreateBuffer2Cmd {
  uint32_t op;
  uint32_t size;
  uint64_t buffer_size;
  uint32_t memory_property;
};
constexpr uint32_t kOP_rcCreateBuffer2 = 10053;
constexpr uint32_t kSize_rcCreateBuffer2 = 20;

constexpr int32_t EGL_SYNC_NATIVE_FENCE_ANDROID = 0x3144;
constexpr int32_t EGL_SYNC_NATIVE_FENCE_FD_ANDROID = 0x3145;
constexpr int32_t EGL_NO_NATIVE_FENCE_FD_ANDROID = -1;

// Encoded rcCreateSyncKHR commands have the following layout:
// - uint32_t  opcode
// - uint32_t  total command size
// - uint32_t  type                  [input argument]
// - uint32_t  byte-size of attribs array
// - int32_t[] attribs               [input argument]
// - uint32_t  byte-size of attribs array
// - int32_t   destroy_when_signaled [input argument]
// - uint32_t  size of size_glsync_out     (output) [const]
// - uint32_t  size of size_syncthread_out (output) [const]
//
// Since the size of attribs array is variable, the size of generated
// command is also variable. So we separate the command into three parts:
// header, attribs array, and footer.
struct __attribute__((__packed__)) CreateSyncKHRCmdHeader {
  uint32_t op;
  uint32_t size;
  uint32_t type;
  uint32_t attribs_size;
};

struct __attribute__((__packed__)) CreateSyncKHRCmdFooter {
  uint32_t attribs_size;
  int32_t destroy_when_signaled;
  uint32_t size_glsync_out;
  uint32_t size_syncthread_out;
};

constexpr uint32_t kOP_rcCreateSyncKHR = 10029;
constexpr uint32_t kSize_rcCreateSyncKHRCmd = 32;
constexpr uint32_t kSize_GlSyncOut = sizeof(uint64_t);
constexpr uint32_t kSize_SyncThreadOut = sizeof(uint64_t);

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_RENDER_CONTROL_COMMANDS_H_
