// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tpm/drivers/tpm/tpm.h"

#include <fidl/fuchsia.hardware.tpmimpl/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/condition.h>

#include <condition_variable>
#include <mutex>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/devices/tpm/drivers/tpm/commands.h"
#include "src/devices/tpm/drivers/tpm/registers.h"

// See Table 22,
// https://www.trustedcomputinggroup.org/wp-content/uploads/PCClientPlatform-TPM-Profile-for-TPM-2-0-v1-03-20-161114_public-review.pdf
enum TpmState {
  kIdle,
  kReady,
  kReception,
  kExecution,
  kCompletion,
  // Special state to indicate test is finished and thread should stop.
  kTeardownTest,
};

constexpr uint16_t kDeviceId = 0xd00d;
constexpr uint16_t kVendorId = 0xfeed;
constexpr uint8_t kRevisionId = 0x4;

using fuchsia_hardware_tpmimpl::wire::RegisterAddress;
class TpmTest : public zxtest::Test, public fidl::WireServer<fuchsia_hardware_tpmimpl::TpmImpl> {
 public:
  TpmTest()
      : loop_(&kAsyncLoopConfigNeverAttachToThread), fake_root_(MockDevice::FakeRootParent()) {}

  void SetUp() override {
    status_.set_tpm_family(tpm::kTpmFamily20).set_sts_valid(1);
    ASSERT_OK(loop_.StartThread("tpm-test-thread"));

    exec_thread_ = std::thread(&TpmTest::ExecThread, this);

    fidl::WireSyncClient<fuchsia_hardware_tpmimpl::TpmImpl> client;
    auto server = fidl::CreateEndpoints(&client.client_end());
    ASSERT_TRUE(server.is_ok());
    fidl::BindServer(loop_.dispatcher(), std::move(server.value()), this);

    ASSERT_TRUE(client.client_end().is_valid());
    auto device = std::make_unique<tpm::TpmDevice>(fake_root_.get(), std::move(client));
    ASSERT_OK(device->DdkAdd(ddk::DeviceAddArgs("tpm")));
    device.release();
  }

  void TearDown() override {
    auto device = fake_root_->GetLatestChild();
    device_async_remove(device);
    mock_ddk::ReleaseFlaggedDevices(fake_root_.get());

    {
      std::scoped_lock lock(state_mutex_);
      state_ = kTeardownTest;
    }
    state_change_.notify_all();
    if (exec_thread_.joinable()) {
      exec_thread_.join();
    }
  }

  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override {
    switch (request->address) {
      case RegisterAddress::kTpmSts: {
        ASSERT_EQ(request->count, 4);
        uint32_t value = status_.reg_value();
        completer.ReplySuccess(
            fidl::VectorView<uint8_t>::FromExternal(reinterpret_cast<uint8_t*>(&value), 4));
        break;
      }
      case RegisterAddress::kTpmDataFifo: {
        ASSERT_LE(request->count, status_.burst_count());
        ASSERT_GT(request->count, 0);
        std::scoped_lock lock(state_mutex_);
        auto amount = std::min(static_cast<size_t>(request->count), fifo_.size());
        completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(fifo_.data(), amount));
        fifo_.erase(fifo_.begin(), fifo_.begin() + amount);
        if (fifo_.empty()) {
          status_.set_data_avail(0);
        }
        break;
      }
      case RegisterAddress::kTpmDidVid: {
        tpm::DidVidReg reg;
        reg.set_device_id(kDeviceId);
        reg.set_vendor_id(kVendorId);
        completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(
            reinterpret_cast<uint8_t*>(reg.reg_value_ptr()), 4));
        break;
      }
      case RegisterAddress::kTpmRid: {
        uint8_t revision = kRevisionId;
        completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(&revision, 1));
        break;
      }
      default: {
        ASSERT_TRUE(false, "unexpected register");
      }
    }
  }

  void HandleStsWrite(uint32_t value) __TA_NO_THREAD_SAFETY_ANALYSIS {
    tpm::StsReg st;
    st.set_reg_value(value);
    if (st.command_ready()) {
      std::scoped_lock lock(state_mutex_);
      if (state_ == kIdle) {
        state_ = kReady;
        fifo_.clear();
        status_.set_command_ready(1).set_burst_count(64);
      } else {
        state_ = kIdle;
        status_.set_command_ready(0);
      }
      state_change_.notify_all();
    } else if (st.tpm_go()) {
      // The spec technically defines setting TPM_GO for all states, but receiving it in any state
      // except reception probably indicates a bug.
      std::scoped_lock lock(state_mutex_);
      ASSERT_EQ(state_, kReception);
      ASSERT_EQ(status_.expect(), 0);
      state_ = kExecution;
      state_change_.notify_all();
    } else {
      ASSERT_FALSE(true, "Unknown bit set in TPM STS register!");
    }
  }

  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override {
    switch (request->address) {
      case RegisterAddress::kTpmSts: {
        ASSERT_EQ(request->data.count(), 4);
        uint32_t value = *reinterpret_cast<uint32_t*>(request->data.mutable_data());
        // There should only be 1 bit set on a TPM write.
        // Leading zeros + trailing zeros should equal 31.
        ASSERT_EQ(__builtin_clz(value) + __builtin_ctz(value), 31);
        HandleStsWrite(value);
        break;
      }
      case RegisterAddress::kTpmDataFifo: {
        std::scoped_lock lock(state_mutex_);
        if (state_ == kReady) {
          state_ = kReception;
          status_.set_expect(1);
          state_change_.notify_all();
        }
        fifo_.insert(fifo_.end(), request->data.begin(), request->data.end());
        UpdateExpect();
        break;
      }
      default: {
        ASSERT_FALSE(true, "Unsupported register");
        break;
      }
    }
    completer.ReplySuccess();
  }

  void UpdateExpect() __TA_REQUIRES(state_mutex_) {
    TpmCmdHeader* hdr = reinterpret_cast<TpmCmdHeader*>(fifo_.data());
    if (be32toh(hdr->command_size) == fifo_.size()) {
      status_.set_expect(0);
    }
  }

  void ExecThread() {
    auto cur_state = TpmState::kIdle;
    std::scoped_lock lock(state_mutex_);
    while (state_ != kTeardownTest) {
      state_change_.wait(state_mutex_, [&cur_state, this]() __TA_REQUIRES(state_mutex_) {
        return state_ != cur_state;
      });
      cur_state = state_;
      switch (state_) {
        case kExecution: {
          std::vector<uint8_t> out;
          TpmCmdHeader* hdr = reinterpret_cast<TpmCmdHeader*>(fifo_.data());
          handle_command_(hdr, out);
          fifo_ = std::move(out);
          state_ = kCompletion;
          status_.set_data_avail(1);
          break;
        }
        default: {
          break;
        }
      }
    }
  }

  fidl::WireSyncClient<fuchsia_tpm::TpmDevice> GetTpmClient() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_tpm::TpmDevice>();
    ZX_ASSERT(endpoints.status_value() == ZX_OK);
    tpm::TpmDevice* tpm = fake_root_->GetLatestChild()->GetDeviceContext<tpm::TpmDevice>();
    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), tpm);
    fidl::WireSyncClient<fuchsia_tpm::TpmDevice> client(std::move(endpoints->client));

    return client;
  }

 protected:
  async::Loop loop_;
  tpm::StsReg status_;
  TpmState state_ __TA_GUARDED(state_mutex_) = TpmState::kIdle;
  std::vector<uint8_t> fifo_ __TA_GUARDED(state_mutex_);
  std::mutex state_mutex_;
  std::condition_variable_any state_change_;
  std::thread exec_thread_;

  std::shared_ptr<MockDevice> fake_root_;
  std::function<void(TpmCmdHeader*, std::vector<uint8_t>&)> handle_command_;
};

TEST_F(TpmTest, TestDdkInit) {
  auto dev = fake_root_->GetLatestChild();
  dev->InitOp();
  ASSERT_OK(dev->WaitUntilInitReplyCalled());
}

TEST_F(TpmTest, TestDdkSuspendToRam) {
  auto dev = fake_root_->GetLatestChild();
  dev->InitOp();
  ASSERT_OK(dev->WaitUntilInitReplyCalled());

  uint16_t shutdown_type = -1;
  handle_command_ = [&shutdown_type](TpmCmdHeader* c, std::vector<uint8_t>& out) {
    ASSERT_EQ(be32toh(c->command_code), TPM_CC_SHUTDOWN);
    ASSERT_EQ(be32toh(c->command_size), sizeof(TpmShutdownCmd));
    TpmShutdownCmd* shutdown = reinterpret_cast<TpmShutdownCmd*>(c);
    shutdown_type = be16toh(shutdown->shutdown_type);

    TpmResponseHeader r{
        .tag = htobe16(TPM_ST_NO_SESSIONS),
        .response_size = htobe32(sizeof(TpmResponseHeader)),
        .response_code = 0,
    };
    out.resize(sizeof(r), 0);
    memcpy(out.data(), &r, sizeof(r));
  };

  dev->SuspendNewOp(DEV_POWER_STATE_D0, false, DEVICE_SUSPEND_REASON_SUSPEND_RAM);
  dev->WaitUntilSuspendReplyCalled();
  ASSERT_EQ(shutdown_type, TPM_SU_STATE);
}

TEST_F(TpmTest, TestDdkSuspendShutdown) {
  auto dev = fake_root_->GetLatestChild();
  dev->InitOp();
  ASSERT_OK(dev->WaitUntilInitReplyCalled());

  uint16_t shutdown_type = -1;
  handle_command_ = [&shutdown_type](TpmCmdHeader* c, std::vector<uint8_t>& out) {
    ASSERT_EQ(be32toh(c->command_code), TPM_CC_SHUTDOWN);
    ASSERT_EQ(be32toh(c->command_size), sizeof(TpmShutdownCmd));
    TpmShutdownCmd* shutdown = reinterpret_cast<TpmShutdownCmd*>(c);
    shutdown_type = be16toh(shutdown->shutdown_type);

    TpmResponseHeader r{
        .tag = htobe16(TPM_ST_NO_SESSIONS),
        .response_size = htobe32(sizeof(TpmResponseHeader)),
        .response_code = 0,
    };
    out.resize(sizeof(r), 0);
    memcpy(out.data(), &r, sizeof(r));
  };

  dev->SuspendNewOp(DEV_POWER_STATE_D0, false, DEVICE_SUSPEND_REASON_POWEROFF);
  dev->WaitUntilSuspendReplyCalled();
  ASSERT_EQ(shutdown_type, TPM_SU_CLEAR);
}

TEST_F(TpmTest, TestGetDeviceId) {
  auto dev = fake_root_->GetLatestChild();
  dev->InitOp();
  ASSERT_OK(dev->WaitUntilInitReplyCalled());

  auto tpm = GetTpmClient();
  auto result = tpm.GetDeviceId();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  auto& data = result->result.response();
  ASSERT_EQ(data.device_id, kDeviceId);
  ASSERT_EQ(data.vendor_id, kVendorId);
  ASSERT_EQ(data.revision_id, kRevisionId);
}

TEST_F(TpmTest, TestSendVendorCommand) {
  auto dev = fake_root_->GetLatestChild();
  dev->InitOp();
  ASSERT_OK(dev->WaitUntilInitReplyCalled());

  constexpr uint32_t kTpmVendorCommand = 0x10;
  struct TestCommandRequest {
    TpmCmdHeader hdr;
    uint8_t value;
  } __PACKED;
  struct TestCommandResponse {
    TpmResponseHeader hdr;
    uint8_t value;
  } __PACKED;
  handle_command_ = [](TpmCmdHeader* c, std::vector<uint8_t>& out) {
    ASSERT_EQ(be32toh(c->command_code), tpm::kTpmVendorPrefix | kTpmVendorCommand);
    ASSERT_EQ(be32toh(c->command_size), sizeof(TestCommandRequest));
    TestCommandRequest* cmd = reinterpret_cast<TestCommandRequest*>(c);
    ASSERT_EQ(cmd->value, 0xaa);

    TestCommandResponse r{
        .hdr =
            {
                .tag = htobe16(TPM_ST_NO_SESSIONS),
                .response_size = htobe32(sizeof(TestCommandResponse)),
                .response_code = 0,
            },
        .value = 0x32,
    };
    out.resize(sizeof(r), 0);
    memcpy(out.data(), &r, sizeof(r));
  };

  auto tpm = GetTpmClient();
  uint8_t value = 0xaa;
  auto result = tpm.ExecuteVendorCommand(kTpmVendorCommand,
                                         fidl::VectorView<uint8_t>::FromExternal(&value, 1));
  ASSERT_OK(result.status());
  if (result->result.is_err()) {
    ASSERT_OK(result->result.err());
  }
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().result, 0);
  ASSERT_EQ(result->result.response().data.count(), 1);
  ASSERT_EQ(result->result.response().data[0], 0x32);
}
