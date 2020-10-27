// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish/pipe_device.h"

#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fake_ddk/fidl-helper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include <ddk/protocol/goldfish/pipe.h>
#include <mock/ddktl/protocol/acpi.h>
#include <zxtest/zxtest.h>
namespace goldfish {

namespace {

constexpr uint32_t kGoldfishBtiId = 0x80888088;

constexpr uint32_t kPipeMinDeviceVersion = 2;
constexpr uint32_t kMaxSignalledPipes = 64;

constexpr llcpp::fuchsia::sysmem::HeapType kSysmemHeaps[] = {
    llcpp::fuchsia::sysmem::HeapType::SYSTEM_RAM,
    llcpp::fuchsia::sysmem::HeapType::GOLDFISH_DEVICE_LOCAL,
    llcpp::fuchsia::sysmem::HeapType::GOLDFISH_HOST_VISIBLE,
};

// MMIO Registers of goldfish pipe.
// The layout should match the register offsets defined in pipe_device.cc.
struct Registers {
  uint32_t command;
  uint32_t signal_buffer_high;
  uint32_t signal_buffer_low;
  uint32_t signal_buffer_count;
  uint32_t reserved0[1];
  uint32_t open_buffer_high;
  uint32_t open_buffer_low;
  uint32_t reserved1[2];
  uint32_t version;
  uint32_t reserved2[3];
  uint32_t get_signalled;

  void DebugPrint() const {
    printf(
        "Registers [ command %08x signal_buffer: %08x %08x count %08x open_buffer: %08x %08x "
        "version %08x get_signalled %08x ]\n",
        command, signal_buffer_high, signal_buffer_low, signal_buffer_count, open_buffer_high,
        open_buffer_low, version, get_signalled);
  }
};

// A RAII memory mapping wrapper of VMO to memory.
class VmoMapping {
 public:
  VmoMapping(const zx::vmo& vmo, size_t size, size_t offset = 0,
             zx_vm_option_t perm = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE)
      : vmo_(vmo), size_(size), offset_(offset), perm_(perm) {
    map();
  }

  ~VmoMapping() { unmap(); }

  void map() {
    if (!ptr_) {
      zx::vmar::root_self()->map(perm_, 0, vmo_, offset_, size_,
                                 reinterpret_cast<uintptr_t*>(&ptr_));
    }
  }

  void unmap() {
    if (ptr_) {
      zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(ptr_), size_);
      ptr_ = nullptr;
    }
  }

  void* ptr() const { return ptr_; }

 private:
  const zx::vmo& vmo_;
  size_t size_ = 0u;
  size_t offset_ = 0u;
  zx_vm_option_t perm_ = 0;
  void* ptr_ = nullptr;
};

struct ProtocolDeviceOps {
  const zx_protocol_device_t* ops = nullptr;
  void* ctx = nullptr;
};

// Create our own fake_ddk Bind class. The Binder will have multiple devices
// added (PipeDevice and Pipe Instance Device). Each device will have its own
// FIDL messenger bound to the remote channel.
class Binder : public fake_ddk::Bind {
 public:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status;
    if (parents_.find(parent) != parents_.end()) {
      return ZX_ERR_ALREADY_BOUND;
    }
    parents_.insert(parent);

    if (args && args->ops) {
      if (args->ops->message) {
        // We use parent device as a key to find device
        // FIDL messengers device tree.
        fidl_messengers_[parent] = std::make_unique<fake_ddk::FidlMessenger>();
        auto* fidl = fidl_messengers_[parent].get();

        std::optional<zx::channel> remote_channel = std::nullopt;
        if (args->client_remote) {
          remote_channel = zx::channel(args->client_remote);
        }

        if ((status = fidl->SetMessageOp(args->ctx, args->ops->message,
                                         std::move(remote_channel))) < 0) {
          return status;
        }
      }
    }

    *out = fake_ddk::kFakeDevice;
    add_called_ = true;

    last_ops_.ctx = args->ctx;
    last_ops_.ops = args->ops;
    return ZX_OK;
  }

  ProtocolDeviceOps GetLastDeviceOps() { return last_ops_; }

  const zx::channel& GetFidlChannel(zx_device_t* parent) const {
    return fidl_messengers_.at(parent)->local();
  }

  const std::set<zx_device_t*>& parents() const { return parents_; }

 private:
  std::set</*parent*/ zx_device_t*> parents_;
  std::map</*parent*/ zx_device_t*, std::unique_ptr<fake_ddk::FidlMessenger>> fidl_messengers_;
  ProtocolDeviceOps last_ops_;
};

}  // namespace

// Test suite creating fake PipeDevice on a mock ACPI bus.
class PipeDeviceTest : public zxtest::Test {
 public:
  // |zxtest::Test|
  void SetUp() override {
    zx::bti out_bti;
    ASSERT_OK(fake_bti_create(out_bti.reset_and_get_address()));
    ASSERT_OK(out_bti.duplicate(ZX_RIGHT_SAME_RIGHTS, &acpi_bti_));

    constexpr size_t kCtrlSize = 4096u;
    zx::vmo vmo_control;
    ASSERT_OK(zx::vmo::create(kCtrlSize, 0u, &vmo_control));
    ASSERT_OK(vmo_control.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_control_));

    zx::interrupt irq;
    ASSERT_OK(zx::interrupt::create(zx::resource(), 0u, ZX_INTERRUPT_VIRTUAL, &irq));
    ASSERT_OK(irq.duplicate(ZX_RIGHT_SAME_RIGHTS, &irq_));

    mock_acpi_.ExpectGetBti(ZX_OK, kGoldfishBtiId, 0, std::move(out_bti))
        .ExpectGetMmio(ZX_OK, 0u, {.offset = 0u, .size = kCtrlSize, .vmo = vmo_control.release()})
        .ExpectMapInterrupt(ZX_OK, 0u, std::move(irq));

    mock_acpi_.mock_connect_sysmem().ExpectCallWithMatcher([this](const zx::channel& connection) {
      zx_info_handle_basic_t info;
      connection.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
      sysmem_request_koid_ = info.koid;
      return ZX_OK;
    });

    auto register_heap = [this](uint64_t heap, const zx::channel& connection) -> zx_status_t {
      if (sysmem_heap_request_koids_.find(heap) != sysmem_heap_request_koids_.end()) {
        return ZX_ERR_ALREADY_BOUND;
      }
      if (!connection.is_valid()) {
        return ZX_ERR_BAD_HANDLE;
      }
      zx_info_handle_basic_t info;
      connection.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
      sysmem_heap_request_koids_[heap] = info.koid;
      return ZX_OK;
    };

    for (const auto heap : kSysmemHeaps) {
      uint64_t heap_id = static_cast<uint64_t>(heap);
      mock_acpi_.mock_register_sysmem_heap().ExpectCallWithMatcher(
          [register_heap, heap_id](uint64_t heap, const zx::channel& connection) {
            EXPECT_EQ(heap, heap_id);
            return register_heap(heap, connection);
          });
    }

    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_ACPI,
                    *reinterpret_cast<const fake_ddk::Protocol*>(mock_acpi_.GetProto())};

    ddk_.SetProtocols(std::move(protocols));
    dut_ = std::make_unique<PipeDevice>(fake_ddk::FakeParent());
  }

  // |zxtest::Test|
  void TearDown() override {}

  std::unique_ptr<VmoMapping> MapControlRegisters() const {
    return std::make_unique<VmoMapping>(vmo_control_, /*size=*/sizeof(Registers), /*offset=*/0);
  }

  template <typename T>
  static void Flush(const T* t) {
    zx_cache_flush(t, sizeof(T), ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  }

 protected:
  ddk::MockAcpi mock_acpi_;
  Binder ddk_;
  std::unique_ptr<PipeDevice> dut_;
  ProtocolDeviceOps child_device_ops_;

  zx::bti acpi_bti_;
  zx::vmo vmo_control_;
  zx::interrupt irq_;

  zx_koid_t sysmem_request_koid_ = ZX_KOID_INVALID;
  std::map<uint64_t, zx_koid_t> sysmem_heap_request_koids_;
};

TEST_F(PipeDeviceTest, Bind) {
  {
    auto mapped = MapControlRegisters();
    Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());
    ctrl_regs->version = kPipeMinDeviceVersion;
  }

  ASSERT_OK(dut_->Bind());

  {
    auto mapped = MapControlRegisters();
    Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());
    Flush(ctrl_regs);

    zx_paddr_t signal_buffer = (static_cast<uint64_t>(ctrl_regs->signal_buffer_high) << 32u) |
                               (ctrl_regs->signal_buffer_low);
    ASSERT_NE(signal_buffer, 0u);

    uint32_t buffer_count = ctrl_regs->signal_buffer_count;
    ASSERT_EQ(buffer_count, kMaxSignalledPipes);

    zx_paddr_t open_buffer =
        (static_cast<uint64_t>(ctrl_regs->open_buffer_high) << 32u) | (ctrl_regs->open_buffer_low);
    ASSERT_NE(open_buffer, 0u);
  }

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(PipeDeviceTest, Open) {
  ASSERT_OK(dut_->Bind());

  zx_device_t* instance_dev;
  ASSERT_OK(dut_->DdkOpen(&instance_dev, 0u));
  ASSERT_EQ(instance_dev, fake_ddk::kFakeDevice);
  ASSERT_TRUE(ddk_.parents().find(fake_ddk::kFakeParent) != ddk_.parents().end());
  ASSERT_TRUE(ddk_.parents().find(fake_ddk::kFakeDevice) != ddk_.parents().end());

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(PipeDeviceTest, CreatePipe) {
  ASSERT_OK(dut_->Bind());

  int32_t id;
  zx::vmo vmo;
  ASSERT_OK(dut_->GoldfishPipeCreate(&id, &vmo));
  ASSERT_NE(id, 0u);
  ASSERT_TRUE(vmo.is_valid());

  dut_->GoldfishPipeDestroy(id);

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(PipeDeviceTest, CreatePipeMultiThreading) {
  ASSERT_OK(dut_->Bind());

  auto create_pipe = [this](size_t num_pipes, std::vector<int32_t>* ids) {
    for (size_t i = 0; i < num_pipes; i++) {
      int32_t id;
      zx::vmo vmo;
      dut_->GoldfishPipeCreate(&id, &vmo);
      ids->push_back(id);
    }
  };

  std::vector<int32_t> ids_1, ids_2;
  constexpr size_t kNumPipesPerThread = 1000u;
  std::thread create_pipe_thread_1(
      [create_pipe, ids = &ids_1]() { create_pipe(kNumPipesPerThread, ids); });
  std::thread create_pipe_thread_2(
      [create_pipe, ids = &ids_2]() { create_pipe(kNumPipesPerThread, ids); });
  create_pipe_thread_1.join();
  create_pipe_thread_2.join();

  std::vector<int32_t> set_intersect, set_union;

  std::set_intersection(ids_1.begin(), ids_1.end(), ids_2.begin(), ids_2.end(),
                        std::back_inserter(set_intersect));
  std::set_union(ids_1.begin(), ids_1.end(), ids_2.begin(), ids_2.end(),
                 std::back_inserter(set_union));

  ASSERT_EQ(set_intersect.size(), 0u);
  ASSERT_EQ(set_union.size(), 2 * kNumPipesPerThread);
}

TEST_F(PipeDeviceTest, Exec) {
  ASSERT_OK(dut_->Bind());

  int32_t id;
  zx::vmo vmo;
  ASSERT_OK(dut_->GoldfishPipeCreate(&id, &vmo));
  ASSERT_NE(id, 0u);
  ASSERT_TRUE(vmo.is_valid());

  dut_->GoldfishPipeExec(id);

  {
    auto mapped = MapControlRegisters();
    Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());
    ASSERT_EQ(ctrl_regs->command, static_cast<uint32_t>(id));
  }

  dut_->GoldfishPipeDestroy(id);

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(PipeDeviceTest, TransferObservedSignals) {
  ASSERT_OK(dut_->Bind());

  int32_t id;
  zx::vmo vmo;
  ASSERT_OK(dut_->GoldfishPipeCreate(&id, &vmo));

  zx::event old_event, old_event_dup;
  ASSERT_OK(zx::event::create(0u, &old_event));
  ASSERT_OK(old_event.duplicate(ZX_RIGHT_SAME_RIGHTS, &old_event_dup));
  ASSERT_OK(dut_->GoldfishPipeSetEvent(id, std::move(old_event_dup)));

  // Trigger signals on "old" event.
  old_event.signal(0u, llcpp::fuchsia::hardware::goldfish::SIGNAL_READABLE);

  zx::event new_event, new_event_dup;
  ASSERT_OK(zx::event::create(0u, &new_event));
  // Clear the target signal.
  ASSERT_OK(new_event.signal(llcpp::fuchsia::hardware::goldfish::SIGNAL_READABLE, 0u));
  ASSERT_OK(new_event.duplicate(ZX_RIGHT_SAME_RIGHTS, &new_event_dup));
  ASSERT_OK(dut_->GoldfishPipeSetEvent(id, std::move(new_event_dup)));

  // Wait for `SIGNAL_READABLE` signal on the new event.
  zx_signals_t observed;
  ASSERT_OK(new_event.wait_one(llcpp::fuchsia::hardware::goldfish::SIGNAL_READABLE,
                               zx::time::infinite_past(), &observed));

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(PipeDeviceTest, GetBti) {
  ASSERT_OK(dut_->Bind());

  zx::bti bti;
  ASSERT_OK(dut_->GoldfishPipeGetBti(&bti));

  zx_info_bti_t goldfish_bti_info, acpi_bti_info;
  ASSERT_OK(
      bti.get_info(ZX_INFO_BTI, &goldfish_bti_info, sizeof(goldfish_bti_info), nullptr, nullptr));
  ASSERT_OK(
      acpi_bti_.get_info(ZX_INFO_BTI, &acpi_bti_info, sizeof(acpi_bti_info), nullptr, nullptr));

  ASSERT_FALSE(memcmp(&goldfish_bti_info, &acpi_bti_info, sizeof(zx_info_bti_t)));

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(PipeDeviceTest, ConnectToSysmem) {
  ASSERT_OK(dut_->Bind());

  zx::channel sysmem_server, sysmem_client;
  zx_koid_t server_koid = ZX_KOID_INVALID, client_koid = ZX_KOID_INVALID;
  ASSERT_OK(zx::channel::create(0u, &sysmem_server, &sysmem_client));

  zx_info_handle_basic_t info;
  ASSERT_OK(sysmem_server.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  server_koid = info.koid;
  client_koid = info.related_koid;

  ASSERT_OK(dut_->GoldfishPipeConnectSysmem(std::move(sysmem_server)));
  ASSERT_NE(sysmem_request_koid_, ZX_KOID_INVALID);
  ASSERT_EQ(sysmem_request_koid_, server_koid);

  for (const auto& heap : kSysmemHeaps) {
    zx::channel heap_server, heap_client;
    zx_koid_t server_koid = ZX_KOID_INVALID, client_koid = ZX_KOID_INVALID;
    ASSERT_OK(zx::channel::create(0u, &heap_server, &heap_client));

    zx_info_handle_basic_t info;
    ASSERT_OK(heap_server.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    server_koid = info.koid;
    client_koid = info.related_koid;

    uint64_t heap_id = static_cast<uint64_t>(heap);
    ASSERT_OK(dut_->GoldfishPipeRegisterSysmemHeap(heap_id, std::move(heap_server)));
    ASSERT_TRUE(sysmem_heap_request_koids_.find(heap_id) != sysmem_heap_request_koids_.end());
    ASSERT_NE(sysmem_heap_request_koids_.at(heap_id), ZX_KOID_INVALID);
    ASSERT_EQ(sysmem_heap_request_koids_.at(heap_id), server_koid);
  }

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

}  // namespace goldfish
