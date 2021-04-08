// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tee/drivers/optee/optee-client.h"

#include <endian.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/rpmb/llcpp/fidl.h>
#include <fuchsia/tee/llcpp/fidl.h>
#include <fuchsia/tee/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake-object/object.h>
#include <lib/fake-resource/resource.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fake_ddk/fidl-helper.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/bti.h>
#include <stdlib.h>

#include <ddktl/suspend-txn.h>
#include <zxtest/zxtest.h>

#include "src/devices/tee/drivers/optee/optee-controller.h"
#include "src/devices/tee/drivers/optee/optee-rpmb.h"
#include "src/devices/tee/drivers/optee/optee-smc.h"
#include "src/devices/tee/drivers/optee/tee-smc.h"

namespace optee {
namespace {

namespace frpmb = fuchsia_hardware_rpmb;

constexpr fuchsia_tee::wire::Uuid kOpteeOsUuid = {
    0x486178E0, 0xE7F8, 0x11E3, {0xBC, 0x5E, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B}};

class OpteeClientTestBase : public OpteeControllerBase, public zxtest::Test {
 public:
  OpteeClientTestBase() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ASSERT_OK(loop_.StartThread());

    // Allocate memory for shared memory buffer
    constexpr size_t kSharedMemorySize = 0x20000;

    fake_bti_create(fake_bti_.reset_and_get_address());

    EXPECT_OK(zx::vmo::create_contiguous(fake_bti_, 0x20000, 0, &fake_vmo_));

    EXPECT_OK(fake_bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, fake_vmo_, 0, kSharedMemorySize,
                            &shared_memory_paddr_, 1, &pmt_));

    mmio_buffer_t mmio;
    EXPECT_OK(
        mmio_buffer_init(&mmio, 0, kSharedMemorySize, fake_vmo_.get(), ZX_CACHE_POLICY_CACHED));

    shared_memory_vaddr_ = reinterpret_cast<zx_vaddr_t>(mmio.vaddr);
    EXPECT_OK(SharedMemoryManager::Create(ddk::MmioBuffer(mmio), shared_memory_paddr_,
                                          &shared_memory_manager_));

    auto endpoints = fidl::CreateEndpoints<fuchsia_tee::Application>();
    ASSERT_TRUE(endpoints.is_ok());
    auto [client_end, server_end] = std::move(endpoints.value());
    optee_client_.reset(new OpteeClient(this, fidl::ClientEnd<fuchsia_tee_manager::Provider>(),
                                        optee::Uuid{kOpteeOsUuid}));
    fidl::BindServer<fidl::WireInterface<fuchsia_tee::Application>>(
        loop_.dispatcher(), std::move(server_end), optee_client_.get());
    optee_client_fidl_ = fidl::WireSyncClient<fuchsia_tee::Application>(std::move(client_end));
  }

  SharedMemoryManager::DriverMemoryPool *driver_pool() const override {
    return shared_memory_manager_->driver_pool();
  }

  SharedMemoryManager::ClientMemoryPool *client_pool() const override {
    return shared_memory_manager_->client_pool();
  }

  zx_status_t RpmbConnectServer(fidl::ServerEnd<frpmb::Rpmb> server) const override {
    return ZX_ERR_UNAVAILABLE;
  };

  const GetOsRevisionResult &os_revision() const override { return os_revision_; };

  zx_device_t *GetDevice() const override { return fake_ddk::kFakeParent; };

  void SetUp() override {}

  void TearDown() override {}

 protected:
  GetOsRevisionResult os_revision_{1, 0, 0, 0, 0};
  std::unique_ptr<SharedMemoryManager> shared_memory_manager_;

  zx::bti fake_bti_;
  zx::vmo fake_vmo_;
  zx::pmt pmt_;
  zx_paddr_t shared_memory_paddr_;
  zx_vaddr_t shared_memory_vaddr_;

  std::unique_ptr<OpteeClient> optee_client_;
  fidl::WireSyncClient<fuchsia_tee::Application> optee_client_fidl_;
  async::Loop loop_;
};

class FakeRpmb : public fidl::WireInterface<frpmb::Rpmb> {
 public:
  using RpmbRequestCallback = fbl::Function<void(fuchsia_hardware_rpmb::wire::Request &request,
                                                 RequestCompleter::Sync &completer)>;
  using GetInfoCallback = fbl::Function<void(GetDeviceInfoCompleter::Sync &completer)>;
  FakeRpmb() {}

  void GetDeviceInfo(GetDeviceInfoCompleter::Sync &completer) override {
    if (info_callback_) {
      info_callback_(completer);
    } else {
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }
  };

  void Request(fuchsia_hardware_rpmb::wire::Request request,
               RequestCompleter::Sync &completer) override {
    if (request_callback_) {
      request_callback_(request, completer);
    } else {
      completer.Close(ZX_ERR_NOT_SUPPORTED);
    }
  };

  void Reset() {
    info_callback_ = nullptr;
    request_callback_ = nullptr;
  }
  void SetRequestCallback(RpmbRequestCallback &&callback) {
    request_callback_ = std::move(callback);
  }
  void SetInfoCallback(GetInfoCallback &&callback) { info_callback_ = std::move(callback); }

 private:
  RpmbRequestCallback request_callback_{nullptr};
  GetInfoCallback info_callback_{nullptr};
};

class OpteeClientTestRpmb : public OpteeClientTestBase {
 public:
  static const size_t kMaxParamCount = 4;
  static const size_t kMaxFramesSize = 4096;
  const size_t kMessageSize = 160;

  const int kDefaultSessionId = 1;
  const int kDefaultCommand = 1;

  static constexpr uint8_t kMarker[] = {0xd, 0xe, 0xa, 0xd, 0xb, 0xe, 0xe, 0xf};

  struct MessageRaw {
    MessageHeader hdr;
    MessageParam params[kMaxParamCount];
  };

  OpteeClientTestRpmb() : rpmb_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ASSERT_OK(rpmb_loop_.StartThread());

    // Create fake RPMB
    fake_rpmb_.reset(new FakeRpmb());
  }

  CallResult CallWithMessage(const optee::Message &message, RpcHandler rpc_handler) override {
    size_t offset = message.paddr() - shared_memory_paddr_;

    MessageHeader *hdr = reinterpret_cast<MessageHeader *>(shared_memory_vaddr_ + offset);
    hdr->return_origin = TEEC_ORIGIN_TEE;
    hdr->return_code = TEEC_SUCCESS;

    switch (hdr->command) {
      case Message::Command::kOpenSession: {
        AllocMemory(kMessageSize, &message_paddr_, &message_mem_id_, rpc_handler);
        AllocMemory(kMaxFramesSize, &tx_frames_paddr_, &tx_frames_mem_id_, rpc_handler);
        AllocMemory(kMaxFramesSize, &rx_frames_paddr_, &rx_frames_mem_id_, rpc_handler);

        hdr->session_id = kDefaultSessionId;
        break;
      }
      case Message::Command::kCloseSession: {
        EXPECT_EQ(hdr->session_id, kDefaultSessionId);
        FreeMemory(message_mem_id_, rpc_handler);
        FreeMemory(tx_frames_mem_id_, rpc_handler);
        FreeMemory(rx_frames_mem_id_, rpc_handler);
        break;
      }
      case Message::Command::kInvokeCommand: {
        offset = message_paddr_ - shared_memory_paddr_;
        MessageRaw *rpmb_access = reinterpret_cast<MessageRaw *>(shared_memory_vaddr_ + offset);
        rpmb_access->hdr.command = RpcMessage::Command::kAccessReplayProtectedMemoryBlock;
        rpmb_access->hdr.num_params = 2;

        rpmb_access->params[0].attribute = MessageParam::kAttributeTypeTempMemInput;
        rpmb_access->params[0].payload.temporary_memory.shared_memory_reference = tx_frames_mem_id_;
        rpmb_access->params[0].payload.temporary_memory.buffer = tx_frames_paddr_;
        rpmb_access->params[0].payload.temporary_memory.size = tx_frames_size_;

        rpmb_access->params[1].attribute = MessageParam::kAttributeTypeTempMemOutput;
        rpmb_access->params[1].payload.temporary_memory.shared_memory_reference = rx_frames_mem_id_;
        rpmb_access->params[1].payload.temporary_memory.buffer = rx_frames_paddr_;
        rpmb_access->params[1].payload.temporary_memory.size = rx_frames_size_;

        RpcFunctionArgs args;
        RpcFunctionResult result;
        args.generic.status = kReturnRpcPrefix | kRpcFunctionIdExecuteCommand;
        args.execute_command.msg_mem_id_upper32 = message_mem_id_ >> 32;
        args.execute_command.msg_mem_id_lower32 = message_mem_id_ & 0xFFFFFFFF;

        zx_status_t status = rpc_handler(args, &result);
        if (status != ZX_OK) {
          hdr->return_code = rpmb_access->hdr.return_code;
        }

        break;
      }
      default:
        hdr->return_code = TEEC_ERROR_NOT_IMPLEMENTED;
    }

    return CallResult{.return_code = kReturnOk};
  };

  zx_status_t RpmbConnectServer(fidl::ServerEnd<frpmb::Rpmb> server) const override {
    fidl::BindServer(rpmb_loop_.dispatcher(), std::move(server), fake_rpmb_.get());
    return ZX_OK;
  };

  void SetUp() override {
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
    auto res = optee_client_fidl_.OpenSession2(std::move(parameter_set));
    EXPECT_OK(res.status());
    EXPECT_EQ(res->session_id, kDefaultSessionId);
  }

  void TearDown() override {
    auto res = optee_client_fidl_.CloseSession(kDefaultSessionId);
    EXPECT_OK(res.status());

    message_paddr_ = 0;
    message_mem_id_ = 0;
    tx_frames_paddr_ = 0;
    tx_frames_mem_id_ = 0;
    rx_frames_paddr_ = 0;
    rx_frames_mem_id_ = 0;
    tx_frames_size_ = 0;
    rx_frames_size_ = 0;

    fake_rpmb_->Reset();
  }

 protected:
  void AllocMemory(size_t size, uint64_t *paddr, uint64_t *mem_id, RpcHandler &rpc_handler) {
    RpcFunctionArgs args;
    RpcFunctionResult result;
    args.generic.status = kReturnRpcPrefix | kRpcFunctionIdAllocateMemory;
    args.allocate_memory.size = size;

    EXPECT_OK(rpc_handler(args, &result));

    *paddr = result.allocate_memory.phys_addr_upper32;
    *paddr = (*paddr << 32) | result.allocate_memory.phys_addr_lower32;
    *mem_id = result.allocate_memory.mem_id_upper32;
    *mem_id = (*mem_id << 32) | result.allocate_memory.mem_id_lower32;
    EXPECT_TRUE(*paddr > shared_memory_paddr_);
  }

  void FreeMemory(uint64_t &mem_id, RpcHandler &rpc_handler) {
    RpcFunctionArgs args;
    RpcFunctionResult result;
    args.generic.status = kReturnRpcPrefix | kRpcFunctionIdFreeMemory;
    args.free_memory.mem_id_upper32 = mem_id >> 32;
    args.free_memory.mem_id_lower32 = mem_id & 0xFFFFFFFF;

    EXPECT_OK(rpc_handler(args, &result));
  }

  uint8_t *GetTxBuffer() const {
    size_t offset = tx_frames_paddr_ - shared_memory_paddr_;
    return reinterpret_cast<uint8_t *>(shared_memory_vaddr_ + offset);
  }

  uint8_t *GetRxBuffer() const {
    size_t offset = rx_frames_paddr_ - shared_memory_paddr_;
    return reinterpret_cast<uint8_t *>(shared_memory_vaddr_ + offset);
  }

  uint64_t message_paddr_{0};
  uint64_t message_mem_id_{0};
  uint64_t tx_frames_paddr_{0};
  uint64_t tx_frames_mem_id_{0};
  uint64_t rx_frames_paddr_{0};
  uint64_t rx_frames_mem_id_{0};
  size_t tx_frames_size_{0};
  size_t rx_frames_size_{0};

  std::unique_ptr<FakeRpmb> fake_rpmb_;
  async::Loop rpmb_loop_;
};

TEST_F(OpteeClientTestRpmb, InvalidRequestCommand) {
  rx_frames_size_ = 512;
  tx_frames_size_ = 512;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = 5;

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_BAD_PARAMETERS);
}

TEST_F(OpteeClientTestRpmb, RpmbError) {
  int req_cnt = 0;
  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestKey);

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    req_cnt++;
    completer.ReplyError(ZX_ERR_UNAVAILABLE);
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_ITEM_NOT_FOUND);
  EXPECT_EQ(req_cnt, 1);
}

TEST_F(OpteeClientTestRpmb, RpmbCommunicationError) {
  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestKey);

  fake_rpmb_->SetRequestCallback(
      [&](auto &request, auto &completer) { completer.Close(ZX_ERR_NOT_SUPPORTED); });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_COMMUNICATION);
}

TEST_F(OpteeClientTestRpmb, GetDeviceInfo) {
  tx_frames_size_ = sizeof(RpmbReq);
  rx_frames_size_ = sizeof(RpmbDevInfo);
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdGetDevInfo;

  fake_rpmb_->SetInfoCallback([&](auto &completer) {
    using DeviceInfo = fuchsia_hardware_rpmb::wire::DeviceInfo;
    using EmmcDeviceInfo = fuchsia_hardware_rpmb::wire::EmmcDeviceInfo;

    EmmcDeviceInfo emmc_info = {};
    emmc_info.rpmb_size = 0x74;
    emmc_info.reliable_write_sector_count = 1;

    EmmcDeviceInfo aligned_emmc_info(emmc_info);
    auto emmc_info_ptr = fidl::ObjectView<EmmcDeviceInfo>::FromExternal(&aligned_emmc_info);

    completer.Reply(DeviceInfo::WithEmmcInfo(emmc_info_ptr));
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_SUCCESS);

  RpmbDevInfo *info = reinterpret_cast<RpmbDevInfo *>(GetRxBuffer());
  EXPECT_EQ(info->ret_code, RpmbDevInfo::kRpmbCmdRetOK);
  EXPECT_EQ(info->rpmb_size, 0x74);
  EXPECT_EQ(info->rel_write_sector_count, 1);
}

TEST_F(OpteeClientTestRpmb, GetDeviceInfoWrongFrameSize) {
  tx_frames_size_ = sizeof(RpmbReq) + 1;
  rx_frames_size_ = sizeof(RpmbDevInfo);
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdGetDevInfo;

  printf("Size of RpmbReq %zu, RpmbFrame %zu\n", sizeof(RpmbReq), sizeof(RpmbFrame));

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_BAD_PARAMETERS);
}

TEST_F(OpteeClientTestRpmb, InvalidDataRequest) {
  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = 10;

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_BAD_PARAMETERS);
}

TEST_F(OpteeClientTestRpmb, InvalidDataRequestFrameSize) {
  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame) + 1;
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = 10;

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_BAD_PARAMETERS);
}

TEST_F(OpteeClientTestRpmb, RequestKeyOk) {
  int req_cnt = 0;
  uint8_t data[fuchsia_hardware_rpmb::wire::FRAME_SIZE];

  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestKey);
  memcpy(rpmb_req->frames->stuff, kMarker, sizeof(kMarker));

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    if (req_cnt == 0) {  // first call
      EXPECT_EQ(request.tx_frames.size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);
      EXPECT_FALSE(request.rx_frames);

      EXPECT_OK(request.tx_frames.vmo.read(data, request.tx_frames.offset, sizeof(kMarker)));
      EXPECT_EQ(memcmp(data, kMarker, sizeof(kMarker)), 0);

    } else if (req_cnt == 1) {  // second call
      EXPECT_EQ(request.tx_frames.size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);
      EXPECT_TRUE(request.rx_frames);
      EXPECT_EQ(request.rx_frames->size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);

      EXPECT_OK(request.tx_frames.vmo.read(data, request.tx_frames.offset, sizeof(data)));
      RpmbFrame *frame = reinterpret_cast<RpmbFrame *>(data);
      EXPECT_EQ(frame->request, htobe16(RpmbFrame::kRpmbRequestStatus));
      EXPECT_OK(request.rx_frames->vmo.write(kMarker, request.rx_frames->offset, sizeof(kMarker)));
    }
    req_cnt++;

    completer.ReplySuccess();
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_SUCCESS);
  EXPECT_EQ(req_cnt, 2);
  EXPECT_EQ(memcmp(GetRxBuffer(), kMarker, sizeof(kMarker)), 0);
}

TEST_F(OpteeClientTestRpmb, RequestKeyInvalid) {
  int req_cnt = 0;
  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE * 2;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestKey);

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    req_cnt++;
    completer.ReplySuccess();
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_BAD_PARAMETERS);
  EXPECT_EQ(req_cnt, 0);
}

TEST_F(OpteeClientTestRpmb, RequestWCounterOk) {
  int req_cnt = 0;
  uint8_t data[sizeof(kMarker)];

  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestWCounter);
  memcpy(rpmb_req->frames->stuff, kMarker, sizeof(kMarker));

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    EXPECT_EQ(request.tx_frames.size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);
    EXPECT_TRUE(request.rx_frames);
    EXPECT_EQ(request.rx_frames->size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);

    EXPECT_OK(request.tx_frames.vmo.read(data, request.tx_frames.offset, sizeof(kMarker)));
    EXPECT_EQ(memcmp(data, kMarker, sizeof(kMarker)), 0);
    EXPECT_OK(request.rx_frames->vmo.write(kMarker, request.rx_frames->offset, sizeof(kMarker)));
    req_cnt++;

    completer.ReplySuccess();
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_SUCCESS);
  EXPECT_EQ(req_cnt, 1);
  EXPECT_EQ(memcmp(GetRxBuffer(), kMarker, sizeof(kMarker)), 0);
}

TEST_F(OpteeClientTestRpmb, RequestWCounterInvalid) {
  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE * 2;
  int req_cnt = 0;

  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestWCounter);

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    req_cnt++;
    completer.ReplySuccess();
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_BAD_PARAMETERS);
  EXPECT_EQ(req_cnt, 0);
}

TEST_F(OpteeClientTestRpmb, ReadDataOk) {
  int req_cnt = 0;
  uint8_t data[sizeof(kMarker)];

  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE * 2;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestReadData);
  memcpy(rpmb_req->frames->stuff, kMarker, sizeof(kMarker));

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    EXPECT_EQ(request.tx_frames.size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);
    EXPECT_TRUE(request.rx_frames);

    EXPECT_OK(request.tx_frames.vmo.read(data, request.tx_frames.offset, sizeof(kMarker)));
    EXPECT_EQ(memcmp(data, kMarker, sizeof(kMarker)), 0);
    EXPECT_OK(request.rx_frames->vmo.write(kMarker, request.rx_frames->offset, sizeof(kMarker)));
    req_cnt++;

    completer.ReplySuccess();
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_SUCCESS);
  EXPECT_EQ(req_cnt, 1);
  EXPECT_EQ(memcmp(GetRxBuffer(), kMarker, sizeof(kMarker)), 0);
}

TEST_F(OpteeClientTestRpmb, RequestReadInvalid) {
  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame) + fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  int req_cnt = 0;

  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestReadData);

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    req_cnt++;
    completer.ReplySuccess();
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_BAD_PARAMETERS);
  EXPECT_EQ(req_cnt, 0);
}

TEST_F(OpteeClientTestRpmb, WriteDataOk) {
  int req_cnt = 0;
  uint8_t data[fuchsia_hardware_rpmb::wire::FRAME_SIZE];

  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE;
  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestWriteData);
  memcpy(rpmb_req->frames->stuff, kMarker, sizeof(kMarker));

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    if (req_cnt == 0) {  // first call
      EXPECT_EQ(request.tx_frames.size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);
      EXPECT_FALSE(request.rx_frames);

      EXPECT_OK(request.tx_frames.vmo.read(data, request.tx_frames.offset, sizeof(kMarker)));
      EXPECT_EQ(memcmp(data, kMarker, sizeof(kMarker)), 0);

    } else if (req_cnt == 1) {  // second call
      EXPECT_EQ(request.tx_frames.size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);
      EXPECT_TRUE(request.rx_frames);
      EXPECT_EQ(request.rx_frames->size, fuchsia_hardware_rpmb::wire::FRAME_SIZE);

      EXPECT_OK(request.tx_frames.vmo.read(data, request.tx_frames.offset, sizeof(data)));
      RpmbFrame *frame = reinterpret_cast<RpmbFrame *>(data);
      EXPECT_EQ(frame->request, htobe16(RpmbFrame::kRpmbRequestStatus));
      EXPECT_OK(request.rx_frames->vmo.write(kMarker, request.rx_frames->offset, sizeof(kMarker)));
    }
    req_cnt++;

    completer.ReplySuccess();
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_SUCCESS);
  EXPECT_EQ(req_cnt, 2);
  EXPECT_EQ(memcmp(GetRxBuffer(), kMarker, sizeof(kMarker)), 0);
}

TEST_F(OpteeClientTestRpmb, RequestWriteInvalid) {
  tx_frames_size_ = sizeof(RpmbReq) + sizeof(RpmbFrame);
  rx_frames_size_ = fuchsia_hardware_rpmb::wire::FRAME_SIZE * 2;
  int req_cnt = 0;

  RpmbReq *rpmb_req = reinterpret_cast<RpmbReq *>(GetTxBuffer());
  rpmb_req->cmd = RpmbReq::kCmdDataRequest;

  rpmb_req->frames->request = htobe16(RpmbFrame::kRpmbRequestWriteData);

  fake_rpmb_->SetRequestCallback([&](auto &request, auto &completer) {
    req_cnt++;
    completer.ReplySuccess();
  });

  fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set;
  auto res = optee_client_fidl_.InvokeCommand(kDefaultSessionId, kDefaultCommand,
                                              std::move(parameter_set));
  EXPECT_OK(res.status());
  EXPECT_EQ(res->op_result.return_code(), TEEC_ERROR_BAD_PARAMETERS);
  EXPECT_EQ(req_cnt, 0);
}

}  // namespace
}  // namespace optee
