// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/debugdata/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fit/defer.h>
#include <lib/standalone-test/standalone.h>
#include <lib/stdcompat/array.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/handle.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/rights.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <cstdint>
#include <string_view>

#include <zxtest/zxtest.h>

namespace {

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_koid_t GetRelatedKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.related_koid : ZX_KOID_INVALID;
}

class SvcSingleProcessTest : public zxtest::Test {
 public:
  void SetUp() final {
    static zx::channel svc_stash;
    static zx::channel svc_server;

    // Prevents multiple iterations from discarding this.
    if (!svc_stash) {
      svc_stash.reset(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    }

    if (!svc_server) {
      ASSERT_TRUE(svc_stash);
      uint32_t actual_handles = 0;
      uint32_t actual_bytes = 0;
      fuchsia_boot_SvcStashStoreRequestMessage request = {};
      ASSERT_OK(svc_stash.read(0, &request, svc_server.reset_and_get_address(), sizeof(request), 1,
                               &actual_bytes, &actual_handles),
                "actual_bytes %zu actual_handles %zu\n", static_cast<size_t>(actual_bytes),
                static_cast<size_t>(actual_handles));

      ASSERT_EQ(actual_bytes, sizeof(request));
      ASSERT_EQ(actual_handles, 1);

      ASSERT_EQ(request.hdr.magic_number, kFidlWireFormatMagicNumberInitial);
      ASSERT_EQ(request.hdr.ordinal, fuchsia_boot_SvcStashStoreOrdinal);
      ASSERT_EQ(request.svc_endpoint, FIDL_HANDLE_PRESENT);
      ASSERT_TRUE(svc_server);
    }

    svc_stash_ = svc_stash.borrow();
    svc_ = standalone::GetNsDir("/svc");
    stashed_svc_ = svc_server.borrow();
  }

  zx::unowned_channel svc_stash() { return svc_stash_->borrow(); }
  zx::unowned_channel local_svc() { return svc_->borrow(); }
  zx::unowned_channel stashed_svc() { return stashed_svc_->borrow(); }

 private:
  zx::unowned_channel svc_stash_;
  zx::unowned_channel svc_;
  zx::unowned_channel stashed_svc_;
};

TEST_F(SvcSingleProcessTest, SvcStubIsValidHandle) { ASSERT_TRUE(local_svc()->is_valid()); }

TEST_F(SvcSingleProcessTest, SvcStashIsValidHandle) {
  ASSERT_TRUE(svc_stash()->is_valid());
  ASSERT_TRUE(stashed_svc()->is_valid());
}

TEST_F(SvcSingleProcessTest, WritingIntoSvcShowsUpInStashHandle) {
  ASSERT_TRUE(local_svc()->is_valid());
  ASSERT_TRUE(stashed_svc()->is_valid());

  auto written_message = cpp20::to_array("Hello World!");
  ASSERT_OK(local_svc()->write(0, written_message.data(), written_message.size(), nullptr, 0));

  decltype(written_message) read_message = {};
  uint32_t actual_bytes, actual_handles;
  ASSERT_OK(stashed_svc()->read(0, read_message.data(), nullptr, read_message.size(), 0,
                                &actual_bytes, &actual_handles));

  EXPECT_EQ(read_message, written_message);
}

TEST_F(SvcSingleProcessTest, SanitizerPublishDataShowsUpInStashedHandle) {
  ASSERT_TRUE(local_svc()->is_valid());
  ASSERT_TRUE(stashed_svc()->is_valid());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(124, 0, &vmo));

  // Channel transitions from not readable to readable.
  zx_signals_t observed = 0;
  ASSERT_STATUS(stashed_svc()->wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), &observed),
                ZX_ERR_TIMED_OUT);

  constexpr const char* kDataSink = "some_sink_name";
  zx_koid_t vmo_koid = GetKoid(vmo.get());

  zx::eventpair token_1(__sanitizer_publish_data(kDataSink, vmo.release()));

  zx_koid_t token_koid = GetRelatedKoid(token_1.get());
  ASSERT_NE(token_koid, ZX_KOID_INVALID);
  ASSERT_NE(vmo_koid, ZX_KOID_INVALID);

  observed = 0;
  ASSERT_OK(stashed_svc()->wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), &observed));
  ASSERT_TRUE((observed & ZX_CHANNEL_READABLE) != 0);

  // There should be an open request after this.
  uint32_t actual_bytes, actual_handles;
  ASSERT_NOT_OK(stashed_svc()->read(0, nullptr, nullptr, 0, 0, &actual_bytes, &actual_handles));

  ASSERT_GT(actual_bytes, 0);
  ASSERT_GT(actual_handles, 0);

  std::vector<uint8_t> message(actual_bytes, 0);
  std::vector<zx_handle_t> handles(actual_handles, ZX_HANDLE_INVALID);

  ASSERT_OK(
      stashed_svc()->read(0, message.data(), handles.data(), static_cast<uint32_t>(message.size()),
                          static_cast<uint32_t>(handles.size()), &actual_bytes, &actual_handles));
  auto close_handles = fit::defer([&]() { zx_handle_close_many(handles.data(), handles.size()); });

  ASSERT_EQ(actual_bytes, message.size());
  ASSERT_EQ(actual_handles, handles.size());
  ASSERT_EQ(actual_handles, 1);
  close_handles.cancel();

  zx::handle debug_data_channel(handles[0]);

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

  // Check that an open request to the DebugDataPublisher exists.
  auto* open_rq = reinterpret_cast<fuchsia_io_DirectoryOpenRequest*>(message.data());

  ASSERT_GE(actual_bytes, sizeof(*open_rq));
  ASSERT_EQ(reinterpret_cast<uintptr_t>(open_rq->path.data), FIDL_ALLOC_PRESENT);
  std::string_view expected_path(fuchsia_debugdata_Publisher_Name);
  std::string_view actual_path(reinterpret_cast<char*>(message.data() + sizeof(*open_rq)),
                               open_rq->path.size);
  ASSERT_GE(actual_bytes, sizeof(*open_rq) + actual_path.size());
  ASSERT_EQ(actual_path, expected_path);

  // There should be an attached channel.
  zx_info_handle_basic_t debug_data_channel_info = {};
  ASSERT_OK(debug_data_channel.get_info(ZX_INFO_HANDLE_BASIC, &debug_data_channel_info,
                                        sizeof(debug_data_channel_info), nullptr, nullptr));
  ASSERT_EQ(debug_data_channel_info.type, ZX_OBJ_TYPE_CHANNEL);

  // Now check the contents of the message itself, such that the vmo and name match.
  message.clear();
  zx::channel debug_data(debug_data_channel.release());

  ASSERT_NOT_OK(debug_data.read(0, nullptr, nullptr, 0, 0, &actual_bytes, &actual_handles));
  message.resize(actual_bytes, 0);
  handles.resize(actual_handles, ZX_HANDLE_INVALID);

  auto cleanup_debug_data =
      fit::defer([&]() { zx_handle_close_many(handles.data(), handles.size()); });

  ASSERT_OK(debug_data.read(0, message.data(), handles.data(),
                            static_cast<uint32_t>(message.size()),
                            static_cast<uint32_t>(handles.size()), &actual_bytes, &actual_handles));
  auto* publish_rq =
      reinterpret_cast<fuchsia_debugdata_PublisherPublishRequestMessage*>(message.data());
  ASSERT_GE(actual_bytes, sizeof(*publish_rq));
  // 0 -> data, 1 -> token(event pair from the one returned in llvm publish data)
  ASSERT_EQ(actual_handles, 2);
  ASSERT_GE(actual_bytes, publish_rq->data_sink.size + sizeof(*publish_rq));

  // Same data sink.
  std::string_view data_sink(reinterpret_cast<char*>(message.data() + sizeof(*publish_rq)),
                             publish_rq->data_sink.size);
  ASSERT_EQ(data_sink, kDataSink);
  ASSERT_EQ(vmo_koid, GetKoid(handles[0]));
  ASSERT_EQ(token_koid, GetKoid(handles[1]));
}

}  // namespace
