// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/vnext/lib/builder/create_buffer_collection.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {
namespace {

// Gets the koid for a handle.
template <typename T>
zx_koid_t GetKoid(const T& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return info.koid;
}

// Gets the peer koid for a handle.
template <typename T>
zx_koid_t GetPeerKoid(const T& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return info.related_koid;
}

class CreateBufferCollectionTest : public fuchsia::media2::BufferProvider,
                                   public gtest::RealLoopFixture {
 public:
  CreateBufferCollectionTest() : thread_(Thread::CreateForLoop(loop())) {}

  // fuchsia::media2::BufferProvider implementation.
  void CreateBufferCollection(zx::eventpair provider_token, std::string vmo_name,
                              CreateBufferCollectionCallback callback) override {
    provider_token_ = std::move(provider_token);
    vmo_name_ = std::move(vmo_name);
    callback_ = std::move(callback);
  }

  void GetBuffers(zx::eventpair participant_token, fuchsia::media2::BufferConstraints constraints,
                  fuchsia::media2::BufferRights rights, std::string name, uint64_t id,
                  GetBuffersCallback callback) override {
    EXPECT_TRUE(false) << "Unexpected call to GetBuffers";
  }

  void BindSysmemToken(zx::eventpair participant_token, BindSysmemTokenCallback callback) override {
    EXPECT_TRUE(false) << "Unexpected call to BindSysmemToken";
  }

 protected:
  Thread& thread() { return thread_; }
  zx::eventpair& provider_token() { return provider_token_; }
  std::string& vmo_name() { return vmo_name_; }
  CreateBufferCollectionCallback& callback() { return callback_; }

 private:
  Thread thread_;
  zx::eventpair provider_token_;
  std::string vmo_name_;
  CreateBufferCollectionCallback callback_;
};

// Tests |CreateBufferCollection| works under nominal conditions.
TEST_F(CreateBufferCollectionTest, Nominal) {
  auto result = fmlib::CreateBufferCollection(*this);
  EXPECT_TRUE(!!provider_token());
  EXPECT_EQ("graph", vmo_name());
  EXPECT_TRUE(!!callback());

  EXPECT_EQ(GetPeerKoid(provider_token()), GetKoid(result.first));
  EXPECT_EQ(GetPeerKoid(provider_token()), GetKoid(result.second));

  fuchsia::media2::BufferCollectionInfo info;
  info.set_buffer_size(1024);
  info.set_buffer_count(3);
  callback()(fpromise::ok(std::move(info)));
}

}  // namespace
}  // namespace fmlib
