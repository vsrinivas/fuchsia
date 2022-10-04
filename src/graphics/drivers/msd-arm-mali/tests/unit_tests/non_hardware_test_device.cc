// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpu/mali/cpp/banjo.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/inspect/cpp/reader.h>

#include <condition_variable>
#include <mutex>

#include <gtest/gtest.h>

#include "mock/mock_bus_mapper.h"
#include "mock/mock_mmio.h"
#include "src/graphics/drivers/msd-arm-mali/src/msd_arm_device.h"
#include "src/graphics/drivers/msd-arm-mali/src/msd_arm_driver.h"
#include "src/graphics/drivers/msd-arm-mali/src/registers.h"

namespace {
class MaliMockMmioBase : public magma::PlatformMmio {
 public:
  virtual ~MaliMockMmioBase() { free(addr()); }

  uint64_t physical_address() override { return 0; }

 protected:
  MaliMockMmioBase(void* addr, size_t size) : magma::PlatformMmio(addr, size) {}
};

using MaliMockMmio = mali::RegisterIoAdapter<MaliMockMmioBase>;

std::unique_ptr<MaliMockMmio> CreateMockMmio(size_t size) {
  void* data = calloc(1, size);
  return std::make_unique<MaliMockMmio>(data, size);
}

class FakePlatformInterrupt : public magma::PlatformInterrupt {
 public:
  void Signal() override {
    std::lock_guard<std::mutex> lock(lock_);
    signaled_ = true;
    cond_.notify_all();
  }
  bool Wait() override {
    std::unique_lock<std::mutex> lock(lock_);
    cond_.wait(lock, [this]() { return signaled_; });

    return true;
  }
  void Complete() override {}

  uint64_t GetMicrosecondsSinceLastInterrupt() override { return 0; }

 private:
  std::mutex lock_;
  std::condition_variable cond_;
  bool signaled_ = false;
};

class FakePlatformDevice : public magma::PlatformDevice {
 public:
  void* GetDeviceHandle() override { return nullptr; }

  uint32_t GetMmioCount() const override { return 1; }

  bool GetProtocol(uint32_t proto_id, void* proto_out) override { return false; }

  std::unique_ptr<magma::PlatformHandle> GetBusTransactionInitiator() const override {
    return nullptr;
  }

  magma::Status LoadFirmware(const char* filename,
                             std::unique_ptr<magma::PlatformBuffer>* firmware_out,
                             uint64_t* size_out) const override {
    return MAGMA_STATUS_UNIMPLEMENTED;
  }

  virtual std::unique_ptr<magma::PlatformMmio> CpuMapMmio(
      unsigned int index, magma::PlatformMmio::CachePolicy cache_policy) override {
    auto mmio = CreateMockMmio(1024 * 1024);

    // Initialize with the S905D3 GPU ID so protected memory can be enabled.
    registers::GpuId::Get().FromValue(1888681984).WriteTo(mmio.get());
    // Initialize MMIO with enough correct values that the driver can load.
    mmio->Write32(0xff, GpuFeatures::kAsPresentOffset);
    return mmio;
  }

  std::unique_ptr<magma::PlatformInterrupt> RegisterInterrupt(unsigned int index) override {
    return std::make_unique<FakePlatformInterrupt>();
  }
};
class FakePlatformDeviceWithProtocol : public FakePlatformDevice {
 public:
  FakePlatformDeviceWithProtocol(uint32_t proto_id, std::vector<uint8_t> metadata)
      : proto_id_(proto_id), metadata_(metadata) {}
  bool GetProtocol(uint32_t proto_id, void* proto_out) override {
    if (proto_id != proto_id_)
      return DRETF(false, "Bad proto %d", proto_id);
    memcpy(proto_out, metadata_.data(), metadata_.size());
    return true;
  }

 private:
  uint32_t proto_id_;
  std::vector<uint8_t> metadata_;
};

}  // namespace

// These tests are unit testing the functionality of MsdArmDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active,
// and with no hardware backing it.
class TestNonHardwareMsdArmDevice {
 public:
  std::unique_ptr<MsdArmDevice> MakeTestDevice() {
    auto device = std::make_unique<MsdArmDevice>();
    device->Init(std::make_unique<FakePlatformDevice>(), std::make_unique<MockBusMapper>());
    return device;
  }

  void MockDump() {
    auto reg_io = std::make_unique<mali::RegisterIo>(MockMmio::Create(1024 * 1024));

    uint32_t offset = static_cast<uint32_t>(registers::CoreReadyState::CoreType::kShader) +
                      static_cast<uint32_t>(registers::CoreReadyState::StatusType::kReady);
    reg_io->Write32(2, offset);
    reg_io->Write32(5, offset + 4);

    static constexpr uint64_t kFaultAddress = 0xffffffff88888888lu;
    registers::GpuFaultAddress::Get().FromValue(kFaultAddress).WriteTo(reg_io.get());
    registers::GpuFaultStatus::Get().FromValue(5).WriteTo(reg_io.get());
    registers::JobIrqFlags::GetRawStat().FromValue(0).set_failed_slots(1).WriteTo(reg_io.get());

    registers::AsRegisters(7).Status().FromValue(5).WriteTo(reg_io.get());
    registers::AsRegisters(7).FaultStatus().FromValue(12).WriteTo(reg_io.get());
    registers::AsRegisters(7).FaultAddress().FromValue(kFaultAddress).WriteTo(reg_io.get());
    registers::JobSlotRegisters(2).Status().FromValue(10).WriteTo(reg_io.get());
    registers::JobSlotRegisters(1).Head().FromValue(9).WriteTo(reg_io.get());
    registers::JobSlotRegisters(0).Tail().FromValue(8).WriteTo(reg_io.get());
    registers::JobSlotRegisters(0).Config().FromValue(7).WriteTo(reg_io.get());

    MsdArmDevice::DumpState dump_state;
    GpuFeatures features;
    features.address_space_count = 9;
    features.job_slot_count = 7;
    MsdArmDevice::DumpRegisters(features, reg_io.get(), &dump_state);
    bool found = false;
    for (auto& pstate : dump_state.power_states) {
      if (std::string("Shader") == pstate.core_type && std::string("Ready") == pstate.status_type) {
        EXPECT_EQ(0x500000002ul, pstate.bitmask);
        found = true;
      }
    }
    EXPECT_EQ(5u, dump_state.gpu_fault_status);
    EXPECT_EQ(kFaultAddress, dump_state.gpu_fault_address);
    EXPECT_EQ(5u, dump_state.address_space_status[7].status);
    EXPECT_EQ(12u, dump_state.address_space_status[7].fault_status);
    EXPECT_EQ(kFaultAddress, dump_state.address_space_status[7].fault_address);
    EXPECT_EQ(10u, dump_state.job_slot_status[2].status);
    EXPECT_EQ(9u, dump_state.job_slot_status[1].head);
    EXPECT_EQ(8u, dump_state.job_slot_status[0].tail);
    EXPECT_EQ(7u, dump_state.job_slot_status[0].config);
    EXPECT_TRUE(found);
    EXPECT_EQ(1u << 16, dump_state.job_irq_rawstat);
  }

  void ProcessRequest() {
    auto device = MakeTestDevice();
    ASSERT_NE(device, nullptr);

    class TestRequest : public DeviceRequest {
     public:
      TestRequest(std::shared_ptr<bool> processing_complete)
          : processing_complete_(processing_complete) {}

     protected:
      magma::Status Process(MsdArmDevice* device) override {
        *processing_complete_ = true;
        return MAGMA_STATUS_OK;
      }

     private:
      std::shared_ptr<bool> processing_complete_;
    };

    auto processing_complete = std::make_shared<bool>(false);

    auto request = std::make_unique<TestRequest>(processing_complete);
    request->ProcessAndReply(device.get());

    EXPECT_TRUE(processing_complete);
  }

  // Check that if there's a waiting request for the device thread and it's
  // descheduled for a long time for some reason that it doesn't immediately
  // think the GPU's hung before processing the request.
  void HangTimerRequest() {
    auto device = MakeTestDevice();

    ASSERT_NE(device, nullptr);

    class FakeJobScheduler : public JobScheduler {
     public:
      FakeJobScheduler(Owner* owner) : JobScheduler(owner, 3) {}
      ~FakeJobScheduler() override {}
      Clock::duration GetCurrentTimeoutDuration() override {
        if (got_timeout_check_)
          return Clock::duration::max();
        got_timeout_check_ = true;
        return Clock::duration::zero();
      }
      void HandleTimedOutAtoms() override {
        // The first hang check should be aborted since the semaphore pretended to
        // be scheduled.
        EXPECT_TRUE(false);
      }

     private:
      bool got_timeout_check_ = false;
    };
    device->scheduler_ = std::make_unique<FakeJobScheduler>(device.get());

    class FakeSemaphore : public magma::PlatformSemaphore {
     public:
      FakeSemaphore() : real_semaphore_(magma::PlatformSemaphore::Create()) {}
      void Signal() override {
        if (signal_count_++ > 0) {
          // After the first one we need to pass through a signal to ensure
          // the device thread receives its shutdown signal.
          real_semaphore_->Signal();
        }
      }

      void Reset() override {}

      magma::Status WaitNoReset(uint64_t timeout_ms) override {
        // After one time through the loop, pretend that the semaphore is signaled.
        real_semaphore_->Signal();
        return MAGMA_STATUS_OK;
      }

      magma::Status Wait(uint64_t timeout_ms) override { return MAGMA_STATUS_OK; }

      bool WaitAsync(magma::PlatformPort* port, uint64_t key) override {
        return real_semaphore_->WaitAsync(port, key);
      }

      void set_local_id(uint64_t id) override {}

      uint64_t id() const override { return real_semaphore_->id(); }
      uint64_t global_id() const override { return real_semaphore_->global_id(); }

      bool duplicate_handle(uint32_t* handle_out) const override {
        return real_semaphore_->duplicate_handle(handle_out);
      }

     private:
      std::unique_ptr<magma::PlatformSemaphore> real_semaphore_;
      uint32_t signal_count_ = 0;
    };
    auto semaphore = std::make_unique<FakeSemaphore>();
    device->device_request_semaphore_ = std::move(semaphore);

    class TestRequest : public DeviceRequest {
     public:
      TestRequest(std::shared_ptr<std::atomic_bool> processing_complete)
          : processing_complete_(processing_complete) {}
      ~TestRequest() {}

     protected:
      magma::Status Process(MsdArmDevice* device) override {
        *processing_complete_ = true;
        return MAGMA_STATUS_OK;
      }

     private:
      std::shared_ptr<std::atomic_bool> processing_complete_;
    };

    auto processing_complete = std::make_shared<std::atomic_bool>(false);

    std::thread device_thread([&device]() { device->DeviceThreadLoop(); });
    auto request = std::make_unique<TestRequest>(processing_complete);
    device->EnqueueDeviceRequest(std::move(request));
    while (!*processing_complete)
      ;
    device->device_thread_quit_flag_ = true;
    device->device_request_semaphore_->Signal();
    device_thread.join();
    device.reset();

    EXPECT_TRUE(processing_complete);
  }

  void MockExecuteAtom() {
    auto device = MakeTestDevice();
    EXPECT_NE(device, nullptr);
    auto reg_io = device->register_io_.get();
    auto connection = MsdArmConnection::Create(0, device.get());

    auto null_atom =
        std::make_unique<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data(), 0);
    device->scheduler_->EnqueueAtom(std::move(null_atom));
    device->scheduler_->TryToSchedule();

    // Atom has 0 job chain address and should be thrown out.
    EXPECT_EQ(0u, device->scheduler_->GetAtomListSize());

    MsdArmAtom atom(connection, 5, 0, 0, magma_arm_mali_user_data(), 0);
    atom.set_require_cycle_counter();
    device->ExecuteAtomOnDevice(&atom, reg_io);
    EXPECT_EQ(registers::GpuCommand::kCmdCycleCountStart,
              reg_io->Read32(registers::GpuCommand::kOffset));

    constexpr uint32_t kJobSlot = 1;
    auto connection1 = MsdArmConnection::Create(0, device.get());
    MsdArmAtom atom1(connection1, 100, kJobSlot, 0, magma_arm_mali_user_data(), 0);

    device->ExecuteAtomOnDevice(&atom1, reg_io);

    registers::JobSlotRegisters regs(kJobSlot);
    EXPECT_EQ(0xffffffffffffffffu, regs.AffinityNext().ReadFrom(reg_io).reg_value());
    EXPECT_EQ(100u, regs.HeadNext().ReadFrom(reg_io).reg_value());
    constexpr uint32_t kCommandStart = registers::JobSlotCommand::kCommandStart;
    EXPECT_EQ(kCommandStart, regs.CommandNext().ReadFrom(reg_io).reg_value());
    auto config_next = regs.ConfigNext().ReadFrom(reg_io);

    // connection should get address slot 0, and connection1 should get
    // slot 1.
    EXPECT_EQ(1u, config_next.address_space());
    EXPECT_EQ(1u, config_next.start_flush_clean());
    EXPECT_EQ(1u, config_next.start_flush_invalidate());
    EXPECT_EQ(0u, config_next.job_chain_flag());
    EXPECT_EQ(1u, config_next.end_flush_clean());
    EXPECT_EQ(1u, config_next.end_flush_invalidate());
    EXPECT_EQ(0u, config_next.enable_flush_reduction());
    EXPECT_EQ(0u, config_next.disable_descriptor_write_back());
    EXPECT_EQ(8u, config_next.thread_priority());

    EXPECT_EQ(registers::GpuCommand::kCmdCycleCountStart,
              reg_io->Read32(registers::GpuCommand::kOffset));
    device->AtomCompleted(&atom, kArmMaliResultSuccess);
    EXPECT_EQ(registers::GpuCommand::kCmdCycleCountStop,
              reg_io->Read32(registers::GpuCommand::kOffset));
  }

  void MockInitializeQuirks() {
    auto reg_io = std::make_unique<mali::RegisterIo>(MockMmio::Create(1024 * 1024));
    GpuFeatures features;
    features.gpu_id.set_reg_value(0x72120000);

    MsdArmDevice::InitializeHardwareQuirks(&features, reg_io.get());
    EXPECT_EQ(1u << 17, reg_io->Read32(0xf04));
    features.gpu_id.set_reg_value(0x08201000);  // T820 R1P0
    MsdArmDevice::InitializeHardwareQuirks(&features, reg_io.get());
    EXPECT_EQ(1u << 16, reg_io->Read32(0xf04));
    features.gpu_id.set_reg_value(0x9990000);
    MsdArmDevice::InitializeHardwareQuirks(&features, reg_io.get());
    EXPECT_EQ(0u, reg_io->Read32(0xf04));
  }

  void Inspect() {
    auto driver = MsdArmDriver::Create();
    auto device = driver->CreateDeviceForTesting(std::make_unique<FakePlatformDevice>(),
                                                 std::make_unique<MockBusMapper>());
    ASSERT_TRUE(device);

    constexpr uint64_t kClientId = 123456;
    auto connection = device->Open(kClientId);
    ASSERT_TRUE(connection);
    auto hierarchy = inspect::ReadFromVmo(zx::vmo(driver->DuplicateInspectHandle()));
    auto* dev_node = hierarchy.value().GetByPath({"msd-arm-mali", "device"});
    ASSERT_TRUE(dev_node);
    const auto& children = dev_node->children();
    ASSERT_LE(1u, children.size());
    bool found_child = false;
    for (auto& child : children) {
      if (child.name().find("connection-") != std::string::npos) {
        auto client_id_prop = child.node().get_property<inspect::UintPropertyValue>("client_id");
        ASSERT_TRUE(client_id_prop);
        EXPECT_EQ(kClientId, client_id_prop->value());
        found_child = true;
        break;
      }
    }
    EXPECT_TRUE(found_child);
  }

  static void mali_protocol_handler(void* ctx, mali_properties_t* properties) {
    *properties = {};
    properties->supports_protected_mode = true;
  }

  void MaliProtocol() {
    auto driver = MsdArmDriver::Create();
    auto device = driver->CreateDeviceForTesting(std::make_unique<FakePlatformDevice>(),
                                                 std::make_unique<MockBusMapper>());
    ASSERT_TRUE(device);
    EXPECT_FALSE(device->IsProtectedModeSupported());
    arm_mali_protocol mali_proto{};
    arm_mali_protocol_ops_t ops;
    ops.get_properties = mali_protocol_handler;
    mali_proto.ctx = this;
    mali_proto.ops = &ops;
    std::vector<uint8_t> proto_vec(sizeof(mali_proto));
    memcpy(proto_vec.data(), &mali_proto, proto_vec.size());

    device = driver->CreateDeviceForTesting(std::make_unique<FakePlatformDeviceWithProtocol>(
                                                ZX_PROTOCOL_ARM_MALI, std::move(proto_vec)),
                                            std::make_unique<MockBusMapper>());
    EXPECT_TRUE(device->IsProtectedModeSupported());
  }

  void ResetOnStart() {
    auto driver = MsdArmDriver::Create();
    auto device = driver->CreateDeviceForTesting(std::make_unique<FakePlatformDevice>(),
                                                 std::make_unique<MockBusMapper>());
    ASSERT_TRUE(device);

    EXPECT_EQ(registers::GpuCommand::kCmdSoftReset,
              device->register_io_->Read32(registers::GpuCommand::kOffset));
  }

  static void mali_get_properties_with_callbacks(void* ctx, mali_properties_t* properties) {
    *properties = {};
    properties->supports_protected_mode = true;
    properties->use_protected_mode_callbacks = true;
  }

  static zx_status_t mali_start_exit_protected(void* ctx) {
    static_cast<TestNonHardwareMsdArmDevice*>(ctx)->got_start_exit_protected_ = true;
    return ZX_OK;
  }
  static zx_status_t mali_finish_exit_protected(void* ctx) {
    static_cast<TestNonHardwareMsdArmDevice*>(ctx)->got_finish_exit_protected_ = true;
    return ZX_OK;
  }

  void ProtectedCallbacks() {
    auto driver = MsdArmDriver::Create();
    arm_mali_protocol mali_proto{};
    arm_mali_protocol_ops_t ops;
    ops.get_properties = mali_get_properties_with_callbacks;
    ops.start_exit_protected_mode = mali_start_exit_protected;
    ops.finish_exit_protected_mode = mali_finish_exit_protected;
    mali_proto.ctx = this;
    mali_proto.ops = &ops;
    std::vector<uint8_t> proto_vec(sizeof(mali_proto));
    memcpy(proto_vec.data(), &mali_proto, proto_vec.size());

    auto device = driver->CreateDeviceForTesting(std::make_unique<FakePlatformDeviceWithProtocol>(
                                                     ZX_PROTOCOL_ARM_MALI, std::move(proto_vec)),
                                                 std::make_unique<MockBusMapper>());
    ASSERT_TRUE(device);
    EXPECT_TRUE(device->IsProtectedModeSupported());
    EXPECT_TRUE(got_start_exit_protected_);
    EXPECT_TRUE(device->exiting_protected_mode_flag_);
    device->HandleResetInterrupt();
    EXPECT_FALSE(device->exiting_protected_mode_flag_);
    EXPECT_TRUE(got_finish_exit_protected_);
    // Callbacks should have been used instead of a soft stop command.
    EXPECT_EQ(0u, device->register_io_->Read32(registers::GpuCommand::kOffset));
  }

 private:
  bool got_start_exit_protected_ = false;
  bool got_finish_exit_protected_ = false;
};

TEST(NonHardwareMsdArmDevice, MockDump) {
  TestNonHardwareMsdArmDevice test;
  test.MockDump();
}

TEST(NonHardwareMsdArmDevice, ProcessRequest) {
  TestNonHardwareMsdArmDevice test;
  test.ProcessRequest();
}

TEST(NonHardwareMsdArmDevice, HangTimerRequest) {
  TestNonHardwareMsdArmDevice test;
  test.HangTimerRequest();
}

TEST(NonHardwareMsdArmDevice, MockExecuteAtom) {
  TestNonHardwareMsdArmDevice test;
  test.MockExecuteAtom();
}

TEST(NonHardwareMsdArmDevice, MockInitializeQuirks) {
  TestNonHardwareMsdArmDevice test;
  test.MockInitializeQuirks();
}

TEST(NonHardwareMsdArmDevice, Inspect) {
  TestNonHardwareMsdArmDevice test;
  test.Inspect();
}

TEST(NonHardwareMsdArmDevice, MaliProtocol) {
  TestNonHardwareMsdArmDevice test;
  test.MaliProtocol();
}

TEST(NonHardwareMsdArmDevice, ResetOnStart) {
  TestNonHardwareMsdArmDevice test;
  test.ResetOnStart();
}

TEST(NonHardwareMsdArmDevice, ProtectedCallbacks) {
  TestNonHardwareMsdArmDevice test;
  test.ProtectedCallbacks();
}
