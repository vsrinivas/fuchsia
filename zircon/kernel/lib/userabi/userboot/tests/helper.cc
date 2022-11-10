// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/debugdata/c/fidl.h>
#include <lib/standalone-test/standalone.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include <zxtest/zxtest.h>

// TODO(fxbug.dev/82681): Clean up manual FIDL definitions once there exists a stable way of doing
// this.
struct fuchsia_io_DirectoryOpenRequest {
  FIDL_ALIGNDECL
  fidl_message_header_t hdr;
  uint32_t flags;
  uint32_t mode;
  fidl_string_t path;
  zx_handle_t object;
};

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_koid_t GetPeerKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.related_koid : ZX_KOID_INVALID;
}

Message::~Message() { zx_handle_close_many(handles.data(), handles.size()); }

std::string_view DebugDataMessageView::sink() const {
  const auto* publish_rq =
      reinterpret_cast<const fuchsia_debugdata_PublisherPublishRequestMessage*>(
          message->msg.data());
  ZX_ASSERT(message->msg.size() >= publish_rq->data_sink.size + sizeof(*publish_rq));

  return {reinterpret_cast<const char*>(message->msg.data() + sizeof(*publish_rq)),
          publish_rq->data_sink.size};
}

zx::unowned_vmo DebugDataMessageView::vmo() const {
  ZX_ASSERT(message->handles.size() >= 1);
  return zx::unowned_vmo(message->handles[0]);
}

zx::unowned_eventpair DebugDataMessageView::token() const {
  ZX_ASSERT(message->handles.size() >= 2);
  return zx::unowned_eventpair(message->handles[1]);
}

zx::channel GetSvcStash() { return zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, 0))); }

void GetStashedSvc(zx::unowned_channel svc_stash, zx::channel& svc) {
  ASSERT_TRUE(*svc_stash);
  uint32_t actual_handles = 0;
  uint32_t actual_bytes = 0;
  fuchsia_boot_SvcStashStoreRequestMessage request = {};
  ASSERT_OK(svc_stash->read(0, &request, svc.reset_and_get_address(), sizeof(request), 1,
                            &actual_bytes, &actual_handles),
            "actual_bytes %zu actual_handles %zu\n", static_cast<size_t>(actual_bytes),
            static_cast<size_t>(actual_handles));

  ASSERT_EQ(actual_bytes, sizeof(request));
  ASSERT_EQ(actual_handles, 1);

  ASSERT_EQ(request.hdr.magic_number, kFidlWireFormatMagicNumberInitial);
  ASSERT_EQ(request.hdr.ordinal, fuchsia_boot_SvcStashStoreOrdinal);
  ASSERT_EQ(request.svc_endpoint, FIDL_HANDLE_PRESENT);
  ASSERT_TRUE(svc);
}

void GetDebugDataMessage(zx::unowned_channel svc, Message& msg) {
  zx_signals_t observed = 0;
  // The channel must have contents or we will block for ever.
  ASSERT_OK(svc->wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), &observed));
  ASSERT_TRUE((observed & ZX_CHANNEL_READABLE) != 0);

  // There should be an open request with the server side of the Publisher protocol.
  uint32_t actual_bytes, actual_handles;
  ASSERT_NOT_OK(svc->read(0, nullptr, nullptr, 0, 0, &actual_bytes, &actual_handles));

  ASSERT_GT(actual_bytes, 0);
  ASSERT_GT(actual_handles, 0);

  msg.msg.resize(actual_bytes);
  msg.handles.resize(actual_handles);

  std::fill(msg.msg.begin(), msg.msg.end(), 0);
  std::fill(msg.handles.begin(), msg.handles.end(), ZX_HANDLE_INVALID);

  ASSERT_OK(svc->read(0, msg.msg.data(), msg.handles.data(), static_cast<uint32_t>(msg.msg.size()),
                      static_cast<uint32_t>(msg.handles.size()), &actual_bytes, &actual_handles));

  ASSERT_EQ(actual_bytes, msg.msg.size());
  ASSERT_EQ(actual_handles, msg.handles.size());
  ASSERT_EQ(actual_handles, 1);

  // There should be an attached channel.
  zx_info_handle_basic_t debug_data_channel_info = {};
  zx::handle debug_data_channel(msg.handles[0]);
  ASSERT_OK(debug_data_channel.get_info(ZX_INFO_HANDLE_BASIC, &debug_data_channel_info,
                                        sizeof(debug_data_channel_info), nullptr, nullptr));
  ASSERT_EQ(debug_data_channel_info.type, ZX_OBJ_TYPE_CHANNEL);

  // Now check the contents of the message itself, such that the vmo and name match.
  msg.msg.clear();
  msg.handles.clear();

  // Check that there are messages, and read them.
  zx::channel debug_data(debug_data_channel.release());
  // The channel must have contents or we will block for ever.
  ASSERT_OK(debug_data.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), &observed));
  ASSERT_TRUE((observed & ZX_CHANNEL_READABLE) != 0);

  ASSERT_NOT_OK(debug_data.read(0, nullptr, nullptr, 0, 0, &actual_bytes, &actual_handles));

  msg.msg.resize(actual_bytes);
  msg.handles.resize(actual_handles);
  std::fill(msg.msg.begin(), msg.msg.end(), 0);
  std::fill(msg.handles.begin(), msg.handles.end(), ZX_HANDLE_INVALID);

  ASSERT_OK(
      debug_data.read(0, msg.msg.data(), msg.handles.data(), static_cast<uint32_t>(msg.msg.size()),
                      static_cast<uint32_t>(msg.handles.size()), &actual_bytes, &actual_handles));
  auto* publish_rq =
      reinterpret_cast<fuchsia_debugdata_PublisherPublishRequestMessage*>(msg.msg.data());
  ASSERT_GE(actual_bytes, sizeof(*publish_rq));
  // 0 -> data, 1 -> token(event pair from the one returned in llvm publish data)
  ASSERT_EQ(actual_handles, 2);
  ASSERT_GE(actual_bytes, publish_rq->data_sink.size + sizeof(*publish_rq));
  ASSERT_NE(msg.handles[0], ZX_HANDLE_INVALID);
  ASSERT_NE(msg.handles[1], ZX_HANDLE_INVALID);
}

int main() {
  int argc = 1;
  const char* argv[4] = {"standalone-test"};

  standalone::Option filter = {"--gtest_filter="};
  standalone::Option repeat = {"--gtest_repeat="};
  standalone::GetOptions({filter, repeat});

  if (!filter.option.empty()) {
    argv[argc++] = filter.option.c_str();
  }
  if (!repeat.option.empty()) {
    argv[argc++] = repeat.option.c_str();
  }

  auto res = RUN_ALL_TESTS(argc, const_cast<char**>(argv));
  if (res == 0) {
    printf(BOOT_TEST_SUCCESS_STRING "\n");
  }
  return res;
}
