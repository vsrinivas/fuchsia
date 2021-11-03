// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../optee-controller.h"

#include <fidl/fuchsia.hardware.rpmb/cpp/wire.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake-object/object.h>
#include <lib/fake-resource/resource.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/sync/completion.h>
#include <lib/zx/bti.h>
#include <stdlib.h>
#include <zircon/time.h>

#include <functional>

#include <ddktl/suspend-txn.h>
#include <zxtest/zxtest.h>

#include "../optee-smc.h"
#include "../tee-smc.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

struct SharedMemoryInfo {
  zx_paddr_t address = 0;
  size_t size = 0;
};

// This will be populated once the FakePdev creates the fake contiguous vmo so we can use the
// physical addresses within it.
static SharedMemoryInfo gSharedMemory = {};

constexpr uuid_t kOpteeOsUuid = {
    0x486178E0, 0xE7F8, 0x11E3, {0xBC, 0x5E, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B}};

using SmcCb = std::function<void(const zx_smc_parameters_t*, zx_smc_result_t*)>;
static SmcCb call_with_arg_handler;
static uint32_t call_with_args_count = 0;
static std::mutex handler_mut;

void SetSmcCallWithArgHandler(SmcCb handler) {
  std::lock_guard<std::mutex> lock(handler_mut);
  call_with_arg_handler = std::move(handler);
}

zx_status_t zx_smc_call(zx_handle_t handle, const zx_smc_parameters_t* parameters,
                        zx_smc_result_t* out_smc_result) {
  EXPECT_TRUE(parameters);
  EXPECT_TRUE(out_smc_result);
  switch (parameters->func_id) {
    case tee_smc::kTrustedOsCallUidFuncId:
      out_smc_result->arg0 = optee::kOpteeApiUid_0;
      out_smc_result->arg1 = optee::kOpteeApiUid_1;
      out_smc_result->arg2 = optee::kOpteeApiUid_2;
      out_smc_result->arg3 = optee::kOpteeApiUid_3;
      break;
    case tee_smc::kTrustedOsCallRevisionFuncId:
      out_smc_result->arg0 = optee::kOpteeApiRevisionMajor;
      out_smc_result->arg1 = optee::kOpteeApiRevisionMinor;
      break;
    case optee::kGetOsRevisionFuncId:
      out_smc_result->arg0 = 1;
      out_smc_result->arg1 = 0;
      break;
    case optee::kExchangeCapabilitiesFuncId:
      out_smc_result->arg0 = optee::kReturnOk;
      out_smc_result->arg1 =
          optee::kSecureCapHasReservedSharedMem | optee::kSecureCapCanUsePrevUnregisteredSharedMem;
      break;
    case optee::kGetSharedMemConfigFuncId:
      out_smc_result->arg0 = optee::kReturnOk;
      out_smc_result->arg1 = gSharedMemory.address;
      out_smc_result->arg2 = gSharedMemory.size;
      break;
    case optee::kCallWithArgFuncId: {
      call_with_args_count++;
      SmcCb handler;
      {
        std::lock_guard<std::mutex> lock(handler_mut);
        std::swap(handler, call_with_arg_handler);
      }
      if (handler != nullptr) {
        handler(parameters, out_smc_result);
      } else {
        out_smc_result->arg0 = optee::kReturnOk;
      }
    } break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

namespace optee {
namespace {

class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() {}

  const pdev_protocol_ops_t* proto_ops() const { return &pdev_protocol_ops_; }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    EXPECT_EQ(index, 0);
    constexpr size_t kSecureWorldMemorySize = 0x20000;

    EXPECT_OK(zx::vmo::create_contiguous(*fake_bti_, 0x20000, 0, &fake_vmo_));

    // Briefly pin the vmo to get the paddr for populating the gSharedMemory object
    zx_paddr_t secure_world_paddr;
    zx::pmt pmt;
    EXPECT_OK(fake_bti_->pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, fake_vmo_, 0,
                             kSecureWorldMemorySize, &secure_world_paddr, 1, &pmt));
    // Use the second half of the secure world range to use as shared memory
    gSharedMemory.address = secure_world_paddr + (kSecureWorldMemorySize / 2);
    gSharedMemory.size = kSecureWorldMemorySize / 2;
    EXPECT_OK(pmt.unpin());

    out_mmio->vmo = fake_vmo_.get();
    out_mmio->offset = 0;
    out_mmio->size = kSecureWorldMemorySize;
    return ZX_OK;
  }

  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) {
    zx_status_t status = fake_bti_create(out_bti->reset_and_get_address());
    // Stash an unowned copy of it, for the purposes of creating a contiguous vmo to back the secure
    // world memory
    fake_bti_ = out_bti->borrow();
    return status;
  }

  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    // Just use a fake root resource for now, which is technically eligible for SMC calls. A more
    // appropriate object would be to use the root resource to mint an SMC resource type.
    return fake_root_resource_create(out_resource->reset_and_get_address());
  }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

 private:
  zx::unowned_bti fake_bti_;
  zx::vmo fake_vmo_;
};

class FakeSysmem : public ddk::SysmemProtocol<FakeSysmem> {
 public:
  FakeSysmem() {}

  const sysmem_protocol_ops_t* proto_ops() const { return &sysmem_protocol_ops_; }

  zx_status_t SysmemConnect(zx::channel allocator2_request) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t SysmemRegisterSecureMem(zx::channel tee_connection) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t SysmemUnregisterSecureMem() { return ZX_ERR_NOT_SUPPORTED; }
};

class FakeRpmb : public ddk::RpmbProtocol<FakeRpmb> {
 public:
  FakeRpmb() {}

  const rpmb_protocol_ops_t* proto_ops() const { return &rpmb_protocol_ops_; }
  void RpmbConnectServer(zx::channel server) { call_cnt++; }

  int get_call_count() const { return call_cnt; }

  void reset() { call_cnt = 0; }

 private:
  int call_cnt = 0;
};

class FakeDdkOptee : public zxtest::Test {
 public:
  FakeDdkOptee() : clients_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ASSERT_OK(clients_loop_.StartThread());
    ASSERT_OK(clients_loop_.StartThread());
    ASSERT_OK(clients_loop_.StartThread());
    parent_->AddProtocol(ZX_PROTOCOL_PDEV, pdev_.proto_ops(), &pdev_, "pdev");
    parent_->AddProtocol(ZX_PROTOCOL_SYSMEM, sysmem_.proto_ops(), &sysmem_, "sysmem");
    parent_->AddProtocol(ZX_PROTOCOL_RPMB, rpmb_.proto_ops(), &rpmb_, "rpmb");

    EXPECT_OK(OpteeController::Create(nullptr, parent_.get()));
    optee_ = parent_->GetLatestChild()->GetDeviceContext<OpteeController>();
  }
  void SetUp() override { call_with_args_count = 0; }

 protected:
  FakePDev pdev_;
  FakeSysmem sysmem_;
  FakeRpmb rpmb_;

  std::shared_ptr<MockDevice> parent_ = MockDevice::FakeRootParent();
  OpteeController* optee_ = nullptr;

  async::Loop clients_loop_;
};

TEST_F(FakeDdkOptee, PmtUnpinned) {
  zx_handle_t pmt_handle = optee_->pmt().get();
  EXPECT_NE(pmt_handle, ZX_HANDLE_INVALID);

  EXPECT_TRUE(fake_object::FakeHandleTable().Get(pmt_handle).is_ok());
  EXPECT_EQ(fake_object::HandleType::PMT, fake_object::FakeHandleTable().Get(pmt_handle)->type());

  optee_->zxdev()->SuspendNewOp(DEV_POWER_STATE_D3COLD, false, DEVICE_SUSPEND_REASON_REBOOT);
  EXPECT_FALSE(fake_object::FakeHandleTable().Get(pmt_handle).is_ok());
}

TEST_F(FakeDdkOptee, RpmbTest) {
  rpmb_.reset();

  using Rpmb = fuchsia_hardware_rpmb::Rpmb;

  EXPECT_EQ(optee_->RpmbConnectServer(fidl::ServerEnd<Rpmb>()), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(rpmb_.get_call_count(), 0);

  auto endpoints = fidl::CreateEndpoints<Rpmb>();
  ASSERT_TRUE(endpoints.is_ok());
  auto [client_end, server_end] = std::move(endpoints.value());

  EXPECT_EQ(optee_->RpmbConnectServer(std::move(server_end)), ZX_OK);
  EXPECT_EQ(rpmb_.get_call_count(), 1);
}

TEST_F(FakeDdkOptee, MultiThreadTest) {
  zx::channel tee_app_client[2];
  sync_completion_t completion1;
  sync_completion_t completion2;
  sync_completion_t smc_completion;
  sync_completion_t smc_completion1;
  zx_status_t status;

  for (auto& i : tee_app_client) {
    zx::channel tee_app_server;
    ASSERT_OK(zx::channel::create(0, &i, &tee_app_server));
    zx::channel provider_server;
    zx::channel provider_client;
    ASSERT_OK(zx::channel::create(0, &provider_client, &provider_server));

    optee_->TeeConnectToApplication(&kOpteeOsUuid, std::move(tee_app_server),
                                    std::move(provider_client));
  }

  auto client_end1 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[0]));
  fidl::WireSharedClient fidl_client1(std::move(client_end1), clients_loop_.dispatcher());
  auto client_end2 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[1]));
  fidl::WireSharedClient fidl_client2(std::move(client_end2), clients_loop_.dispatcher());

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      sync_completion_signal(&smc_completion1);
      sync_completion_wait(&smc_completion, ZX_TIME_INFINITE);
      out->arg0 = optee::kReturnOk;
    });
  }
  {
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client1->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion1);
        });
  }
  status = sync_completion_wait(&completion1, ZX_SEC(1));
  EXPECT_EQ(status, ZX_ERR_TIMED_OUT);
  sync_completion_wait(&smc_completion1, ZX_TIME_INFINITE);

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      out->arg0 = optee::kReturnOk;
    });
  }
  {
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client2->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion2);
        });
  }
  sync_completion_wait(&completion2, ZX_TIME_INFINITE);
  sync_completion_signal(&smc_completion);
  sync_completion_wait(&completion1, ZX_TIME_INFINITE);
  EXPECT_EQ(call_with_args_count, 2);
}

TEST_F(FakeDdkOptee, TheadLimitCorrectOrder) {
  zx::channel tee_app_client[2];
  sync_completion_t completion1;
  sync_completion_t completion2;
  sync_completion_t smc_completion;
  zx_status_t status;

  for (auto& i : tee_app_client) {
    zx::channel tee_app_server;
    ASSERT_OK(zx::channel::create(0, &i, &tee_app_server));
    zx::channel provider_server;
    zx::channel provider_client;
    ASSERT_OK(zx::channel::create(0, &provider_client, &provider_server));

    optee_->TeeConnectToApplication(&kOpteeOsUuid, std::move(tee_app_server),
                                    std::move(provider_client));
  }

  auto client_end1 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[0]));
  fidl::WireSharedClient fidl_client1(std::move(client_end1), clients_loop_.dispatcher());
  auto client_end2 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[1]));
  fidl::WireSharedClient fidl_client2(std::move(client_end2), clients_loop_.dispatcher());

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      sync_completion_signal(&smc_completion);
      out->arg0 = optee::kReturnEThreadLimit;
    });
  }
  {
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client1->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion1);
        });
  }

  sync_completion_wait(&smc_completion, ZX_TIME_INFINITE);
  status = sync_completion_wait(&completion1, ZX_SEC(1));
  EXPECT_EQ(status, ZX_ERR_TIMED_OUT);
  EXPECT_EQ(call_with_args_count, 1);
  EXPECT_EQ(optee_->CommandQueueSize(), 1);

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      out->arg0 = optee::kReturnOk;
    });
  }
  {
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client2->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion2);
        });
  }

  sync_completion_wait(&completion2, ZX_TIME_INFINITE);
  sync_completion_wait(&completion1, ZX_TIME_INFINITE);
  EXPECT_EQ(call_with_args_count, 3);
  EXPECT_EQ(optee_->CommandQueueSize(), 0);
  EXPECT_EQ(optee_->CommandQueueWaitSize(), 0);
}

TEST_F(FakeDdkOptee, TheadLimitWrongOrder) {
  zx::channel tee_app_client[3];
  sync_completion_t completion1;
  sync_completion_t completion2;
  sync_completion_t completion3;
  sync_completion_t smc_completion;
  sync_completion_t smc_sleep_completion;

  for (auto& i : tee_app_client) {
    zx::channel tee_app_server;
    ASSERT_OK(zx::channel::create(0, &i, &tee_app_server));
    zx::channel provider_server;
    zx::channel provider_client;
    ASSERT_OK(zx::channel::create(0, &provider_client, &provider_server));

    optee_->TeeConnectToApplication(&kOpteeOsUuid, std::move(tee_app_server),
                                    std::move(provider_client));
  }

  auto client_end1 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[0]));
  fidl::WireSharedClient fidl_client1(std::move(client_end1), clients_loop_.dispatcher());
  auto client_end2 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[1]));
  fidl::WireSharedClient fidl_client2(std::move(client_end2), clients_loop_.dispatcher());
  auto client_end3 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[2]));
  fidl::WireSharedClient fidl_client3(std::move(client_end3), clients_loop_.dispatcher());

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      sync_completion_signal(&smc_completion);
      sync_completion_wait(&smc_sleep_completion, ZX_TIME_INFINITE);
      out->arg0 = optee::kReturnOk;
    });
  }
  {  // first client is just sleeping for a long time (without ThreadLimit)
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client1->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion1);
        });
  }

  sync_completion_wait(&smc_completion, ZX_TIME_INFINITE);
  EXPECT_FALSE(sync_completion_signaled(&completion1));
  EXPECT_EQ(call_with_args_count, 1);
  sync_completion_reset(&smc_completion);

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      sync_completion_signal(&smc_completion);
      out->arg0 = optee::kReturnEThreadLimit;
    });
  }
  {  // 2nd client got ThreadLimit
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client2->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion2);
        });
  }

  sync_completion_wait(&smc_completion, ZX_TIME_INFINITE);
  EXPECT_FALSE(sync_completion_signaled(&completion2));
  EXPECT_EQ(call_with_args_count, 2);
  EXPECT_EQ(optee_->CommandQueueSize(), 2);

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      out->arg0 = optee::kReturnOk;
    });
  }
  {
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client3->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion3);
        });
  }

  sync_completion_wait(&completion3, ZX_TIME_INFINITE);
  sync_completion_wait(&completion2, ZX_TIME_INFINITE);
  EXPECT_EQ(call_with_args_count, 4);
  sync_completion_signal(&smc_sleep_completion);
  sync_completion_wait(&completion1, ZX_TIME_INFINITE);
  EXPECT_EQ(optee_->CommandQueueSize(), 0);
  EXPECT_EQ(optee_->CommandQueueWaitSize(), 0);
}

TEST_F(FakeDdkOptee, TheadLimitWrongOrderCascade) {
  zx::channel tee_app_client[3];
  sync_completion_t completion1;
  sync_completion_t completion2;
  sync_completion_t completion3;
  sync_completion_t smc_completion;
  sync_completion_t smc_sleep_completion1;
  sync_completion_t smc_sleep_completion2;

  for (auto& i : tee_app_client) {
    zx::channel tee_app_server;
    ASSERT_OK(zx::channel::create(0, &i, &tee_app_server));
    zx::channel provider_server;
    zx::channel provider_client;
    ASSERT_OK(zx::channel::create(0, &provider_client, &provider_server));

    optee_->TeeConnectToApplication(&kOpteeOsUuid, std::move(tee_app_server),
                                    std::move(provider_client));
  }

  auto client_end1 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[0]));
  fidl::WireSharedClient fidl_client1(std::move(client_end1), clients_loop_.dispatcher());
  auto client_end2 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[1]));
  fidl::WireSharedClient fidl_client2(std::move(client_end2), clients_loop_.dispatcher());
  auto client_end3 = fidl::ClientEnd<fuchsia_tee::Application>(std::move(tee_app_client[2]));
  fidl::WireSharedClient fidl_client3(std::move(client_end3), clients_loop_.dispatcher());

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      sync_completion_signal(&smc_completion);
      sync_completion_wait(&smc_sleep_completion1, ZX_TIME_INFINITE);
      out->arg0 = optee::kReturnEThreadLimit;
    });
  }
  {  // first client is just sleeping for a long time (without ThreadLimit)
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client1->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion1);
        });
  }

  sync_completion_wait(&smc_completion, ZX_TIME_INFINITE);
  EXPECT_FALSE(sync_completion_signaled(&completion1));
  EXPECT_EQ(call_with_args_count, 1);
  sync_completion_reset(&smc_completion);

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      sync_completion_signal(&smc_completion);
      sync_completion_wait(&smc_sleep_completion2, ZX_TIME_INFINITE);
      out->arg0 = optee::kReturnOk;
    });
  }
  {  // 2nd client got ThreadLimit
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client2->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion2);
        });
  }

  sync_completion_wait(&smc_completion, ZX_TIME_INFINITE);
  EXPECT_FALSE(sync_completion_signaled(&completion2));
  EXPECT_EQ(call_with_args_count, 2);
  EXPECT_EQ(optee_->CommandQueueSize(), 2);

  {
    SetSmcCallWithArgHandler([&](const zx_smc_parameters_t* params, zx_smc_result_t* out) {
      out->arg0 = optee::kReturnOk;
    });
  }
  {
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    fidl_client3->OpenSession2(
        std::move(parameter_set),
        [&](::fidl::WireResponse<::fuchsia_tee::Application::OpenSession2>* resp) {
          sync_completion_signal(&completion3);
        });
  }
  sync_completion_wait(&completion3, ZX_TIME_INFINITE);
  EXPECT_EQ(call_with_args_count, 3);

  sync_completion_signal(&smc_sleep_completion2);
  sync_completion_wait(&completion2, ZX_TIME_INFINITE);
  EXPECT_EQ(call_with_args_count, 3);
  sync_completion_signal(&smc_sleep_completion1);
  sync_completion_wait(&completion1, ZX_TIME_INFINITE);
  EXPECT_EQ(call_with_args_count, 4);

  EXPECT_EQ(optee_->CommandQueueSize(), 0);
  EXPECT_EQ(optee_->CommandQueueWaitSize(), 0);
}

}  // namespace
}  // namespace optee
