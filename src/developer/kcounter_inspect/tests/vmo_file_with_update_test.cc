// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/kcounter_inspect/vmo_file_with_update.h"

#include "fuchsia/kernel/cpp/fidl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/gtest/real_loop_fixture.h"
#include "lib/sys/cpp/testing/service_directory_provider.h"
#include "src/lib/fxl/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace {

// Stub fuchsia.kernel.Counter service that returns canned respo
class StubKcounter : public fuchsia::kernel::Counter {
 public:
  StubKcounter() { EXPECT_EQ(zx::vmo::create(kSize, 0, &vmo_), ZX_OK); }

  void GetInspectVMO(GetInspectVMOCallback callback) override {
    zx::vmo ret;
    EXPECT_EQ(vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &ret), ZX_OK);
    fuchsia::mem::Buffer buffer{.vmo = std::move(ret), .size = kSize};
    callback(ZX_OK, std::move(buffer));
  }

  void UpdateInspectVMO(UpdateInspectVMOCallback callback) override {
    ++update_count_;
    callback(ZX_OK);
  }

  fidl::InterfaceRequestHandler<fuchsia::kernel::Counter> GetHandler() {
    return bindings_.GetHandler(this);
  }

  int update_count() const { return update_count_; }

 private:
  static constexpr size_t kSize = 4096;

  fidl::BindingSet<fuchsia::kernel::Counter> bindings_;
  zx::vmo vmo_;
  int update_count_ = 0;
};

class VmoFileWithUpdateTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    EXPECT_EQ(loop_.StartThread(), ZX_OK);
    service_directory_provider_.reset(
        new ::sys::testing::ServiceDirectoryProvider(loop_.dispatcher()));
    stub_kcounter_.reset(new StubKcounter);
    FXL_CHECK(service_directory_provider_->AddService(stub_kcounter_->GetHandler()) == ZX_OK);
  }

  void TearDown() override {
    loop_.Quit();
    loop_.JoinThreads();
  }

  async::Loop loop_{&kAsyncLoopConfigNoAttachToThread};
  std::unique_ptr<::sys::testing::ServiceDirectoryProvider> service_directory_provider_;
  std::unique_ptr<StubKcounter> stub_kcounter_;
  thrd_t thread_;
};

TEST_F(VmoFileWithUpdateTest, DoNothing) { VmoFileWithUpdate noop(zx::vmo(), 0, 0, nullptr); }

TEST_F(VmoFileWithUpdateTest, EnsureUpdateCalled) {
  fuchsia::kernel::CounterSyncPtr kcounter;
  ASSERT_EQ(service_directory_provider_->service_directory()->Connect<fuchsia::kernel::Counter>(
                kcounter.NewRequest()),
            ZX_OK);

  zx_status_t status;
  fuchsia::mem::Buffer buffer;
  EXPECT_EQ(kcounter->GetInspectVMO(&status, &buffer), ZX_OK);

  auto vmo_file =
      std::make_unique<VmoFileWithUpdate>(std::move(buffer.vmo), 0, buffer.size, &kcounter);
  EXPECT_EQ(stub_kcounter_->update_count(), 0);

  std::vector<uint8_t> data;
  EXPECT_EQ(vmo_file->ReadAt(128 /*any amount, doesn't really matter*/, 0, &data), ZX_OK);
  EXPECT_EQ(stub_kcounter_->update_count(), 1);

  fuchsia::io::NodeInfo node_info;
  vmo_file->Describe(&node_info);
  EXPECT_EQ(stub_kcounter_->update_count(), 2);
}

}  // namespace
