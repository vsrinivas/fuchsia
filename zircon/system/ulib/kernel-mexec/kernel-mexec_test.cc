#include <zxtest/zxtest.h>

#include <vector>

#include <lib/kernel-mexec/kernel-mexec.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/assert.h>
#include <lib/fidl-async/bind.h>
#include <lib/svc/outgoing.h>
#include <lib/zx/vmar.h>
#include <libzbi/zbi-cpp.h>

namespace {

class FakeSysCalls {
 public:
  FakeSysCalls() : zbi(zbi_data, kZbiSize) {}

  internal::MexecSysCalls Interface() {
    return {
        .mexec =
            [this](zx_handle_t root, zx_handle_t kernel, zx_handle_t bootdata) {
              size_t kernel_size = 0;
              auto status = zx_vmo_get_size(kernel, &kernel_size);
              if (status != ZX_OK)
                return status;

              received_kernel.resize(kernel_size);
              status = zx_vmo_read(kernel, received_kernel.data(), 0, kernel_size);
              if (status != ZX_OK)
                return status;

              size_t bootdata_size = 0;
              status = zx_vmo_get_size(bootdata, &bootdata_size);
              if (status != ZX_OK)
                return status;

              received_bootdata.resize(bootdata_size);
              status = zx_vmo_read(bootdata, received_bootdata.data(), 0, bootdata_size);
              if (status != ZX_OK)
                return status;

              return ZX_OK;
            },
        .mexec_payload_get =
            [this](zx_handle_t root, void* data, size_t size) {
              memcpy(data, zbi_data, std::min(size, kZbiSize));
              return ZX_OK;
            },
    };
  }

  std::vector<uint8_t> received_kernel;
  std::vector<uint8_t> received_bootdata;

  static constexpr size_t kZbiSize = 512;
  uint8_t zbi_data[kZbiSize];

  zbi::Zbi zbi;
};

class MexecTest : public zxtest::Test {
 public:
  MexecTest() {
    ops_.Suspend = [](void* ctx, uint32_t flags, fidl_txn_t* txn) {
      return fuchsia_device_manager_AdministratorSuspend_reply(
          txn, reinterpret_cast<MexecTest*>(ctx)->suspend_callback_(flags));
    };
  }

  static void SetUpTestCase() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ZX_ASSERT(ZX_OK == loop_->StartThread("MexecTestLoop"));
  }

  static void TearDownTestCase() { loop_ = nullptr; }

 protected:
  void SetUp() override {
    outgoing_ = std::make_unique<svc::Outgoing>(loop_->dispatcher());

    ASSERT_OK(zx::vmo::create(1024, 0, &kernel_));
    ASSERT_OK(zx::vmo::create(1024, 0, &bootdata_));

    ASSERT_EQ(ZBI_RESULT_OK, sys_calls_.zbi.Reset());
    ASSERT_EQ(ZBI_RESULT_OK, sys_calls_.zbi.Check(nullptr));

    ASSERT_OK(bootdata_.write(sys_calls_.zbi_data, 0, 512));

    zx::channel listening;
    ASSERT_OK(zx::channel::create(0, &suspend_service_, &listening));

    context_.devmgr_channel = zx::unowned_channel(suspend_service_);

    const auto service = [this](zx::channel request) {
      return fidl_bind(
          loop_->dispatcher(), request.release(),
          reinterpret_cast<fidl_dispatch_t*>(fuchsia_device_manager_Administrator_dispatch), this,
          &ops_);
    };

    outgoing_->root_dir()->AddEntry(fuchsia_device_manager_Administrator_Name,
                                    fbl::MakeRefCounted<fs::Service>(service));
    outgoing_->Serve(std::move(listening));
  }

  KernelMexecContext context_;
  FakeSysCalls sys_calls_;

  zx::channel suspend_service_;
  std::function<zx_status_t(uint32_t)> suspend_callback_ = [](uint32_t) { return ZX_OK; };
  fuchsia_device_manager_Administrator_ops_t ops_;

  std::unique_ptr<svc::Outgoing> outgoing_;

  zx::vmo kernel_;
  zx::vmo bootdata_;

  static std::unique_ptr<async::Loop> loop_;
};

std::unique_ptr<async::Loop> MexecTest::loop_;

static const std::string kKernelText("I'M A KERNEL!");

struct CheckConditions {
  bool has_crash_log = false;
  bool has_others = false;
};

}  // namespace

TEST_F(MexecTest, Success) {
  void* data = nullptr;
  ASSERT_EQ(ZBI_RESULT_OK, sys_calls_.zbi.CreateSection(50, ZBI_TYPE_CRASHLOG, 0, 0, &data));

  ASSERT_OK(kernel_.write(kKernelText.c_str(), 0, kKernelText.length()));

  auto status = internal::PerformMexec(static_cast<void*>(&context_), kernel_.get(),
                                       bootdata_.get(), sys_calls_.Interface());

  // BAD_STATE is expected. Normally the mexec syscall should not return so
  // this method should never return.
  ASSERT_EQ(ZX_ERR_BAD_STATE, status);

  // Ensure the kernel VMO still has the same kernel data.
  ASSERT_GE(sys_calls_.received_kernel.size(), kKernelText.length());
  ASSERT_BYTES_EQ(kKernelText.c_str(), sys_calls_.received_kernel.data(), kKernelText.length());

  // Ensure the bootdata is still valid and has the zbi from mexec_payload_get
  // added to it.
  zbi::Zbi received_zbi(sys_calls_.received_bootdata.data());
  ASSERT_EQ(ZBI_RESULT_OK, received_zbi.Check(nullptr));

  CheckConditions conditions;
  ASSERT_EQ(ZBI_RESULT_OK, received_zbi.ForEach(
                               [](zbi_header_t* hdr, void*, void* ctx) {
                                 auto* conditions = reinterpret_cast<CheckConditions*>(ctx);
                                 if (hdr->type == ZBI_TYPE_CRASHLOG) {
                                   conditions->has_crash_log = true;
                                 } else {
                                   fprintf(stderr, "Found other zbi entry: %d\n", hdr->type);
                                   conditions->has_others = true;
                                 }
                                 return ZBI_RESULT_OK;
                               },
                               static_cast<void*>(&conditions)));

  ASSERT_EQ(true, conditions.has_crash_log);
  ASSERT_EQ(false, conditions.has_others);
}

TEST_F(MexecTest, SuspendFail) {
  // Use an error that is unlikely to occur otherwise.
  const auto error = ZX_ERR_ADDRESS_UNREACHABLE;
  suspend_callback_ = [](uint32_t) { return error; };

  auto status = internal::PerformMexec(static_cast<void*>(&context_), kernel_.get(),
                                       bootdata_.get(), sys_calls_.Interface());

  ASSERT_EQ(error, status);
}

TEST_F(MexecTest, MexecPayloadFail) {
  // Use an error that is unlikely to occur otherwise.
  const auto error = ZX_ERR_ADDRESS_UNREACHABLE;

  auto sys_calls = sys_calls_.Interface();
  sys_calls.mexec_payload_get = [](zx_handle_t, void* buffer, size_t size) { return error; };

  auto status = internal::PerformMexec(static_cast<void*>(&context_), kernel_.get(),
                                       bootdata_.get(), sys_calls);
  ASSERT_EQ(error, status);
}

TEST_F(MexecTest, MexecPayloadJunk) {
  auto sys_calls = sys_calls_.Interface();
  sys_calls.mexec_payload_get = [](zx_handle_t, void* buffer, size_t size) {
    char* char_buffer = static_cast<char*>(buffer);
    for (size_t i = 0; i < size; i++) {
      *(char_buffer++) = 0xA;
    }
    return ZX_OK;
  };

  auto status = internal::PerformMexec(static_cast<void*>(&context_), kernel_.get(),
                                       bootdata_.get(), sys_calls);

  ASSERT_NE(ZX_ERR_BAD_STATE, status);
}
