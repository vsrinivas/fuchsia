// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_control/control_device.h"

#include <fuchsia/hardware/goldfish/addressspace/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/control/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake-object/object.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/rights.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <gtest/gtest.h>

#include "src/graphics/drivers/misc/goldfish_control/render_control_commands.h"

#define ASSERT_OK(expr) ASSERT_EQ(ZX_OK, expr)
#define EXPECT_OK(expr) EXPECT_EQ(ZX_OK, expr)

namespace goldfish {
namespace {

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

class FakeComposite : public ddk::CompositeProtocol<FakeComposite> {
 public:
  explicit FakeComposite(zx_device_t* parent)
      : proto_({&composite_protocol_ops_, this}), parent_(parent) {}

  const composite_protocol_t* proto() const { return &proto_; }

  uint32_t CompositeGetFragmentCount() { return static_cast<uint32_t>(kNumFragments); }

  void CompositeGetFragments(composite_device_fragment_t* comp_list, size_t comp_count,
                             size_t* comp_actual) {
    size_t comp_cur;

    for (comp_cur = 0; comp_cur < comp_count; comp_cur++) {
      strncpy(comp_list[comp_cur].name, "unamed-fragment", 32);
      comp_list[comp_cur].device = parent_;
    }

    if (comp_actual != nullptr) {
      *comp_actual = comp_cur;
    }
  }

  bool CompositeGetFragment(const char* name, zx_device_t** out) {
    *out = parent_;
    return true;
  }

 private:
  static constexpr size_t kNumFragments = 2;

  composite_protocol_t proto_;
  zx_device_t* parent_;
};

class FakePipe : public ddk::GoldfishPipeProtocol<FakePipe, ddk::base_protocol> {
 public:
  struct HeapInfo {
    zx::channel channel;
    bool is_registered = false;
    bool cpu_supported = false;
    bool ram_supported = false;
    bool inaccessible_supported = false;
  };

  FakePipe() : proto_({&goldfish_pipe_protocol_ops_, this}) {}

  const goldfish_pipe_protocol_t* proto() const { return &proto_; }

  zx_status_t GoldfishPipeCreate(int32_t* out_id, zx::vmo* out_vmo) {
    *out_id = kPipeId;
    zx_status_t status = zx::vmo::create(PAGE_SIZE, 0u, out_vmo);
    if (status != ZX_OK) {
      return status;
    }
    status = out_vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &pipe_cmd_buffer_);
    if (status != ZX_OK) {
      return status;
    }

    pipe_created_ = true;
    return ZX_OK;
  }

  zx_status_t GoldfishPipeSetEvent(int32_t id, zx::event pipe_event) {
    if (id != kPipeId) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (!pipe_event.is_valid()) {
      return ZX_ERR_BAD_HANDLE;
    }
    pipe_event_ = std::move(pipe_event);
    return ZX_OK;
  }

  void GoldfishPipeDestroy(int32_t id) { pipe_cmd_buffer_.reset(); }

  void GoldfishPipeOpen(int32_t id) {
    auto mapping = MapCmdBuffer();
    reinterpret_cast<pipe_cmd_buffer_t*>(mapping->ptr())->status = 0;

    pipe_opened_ = true;
  }

  void GoldfishPipeExec(int32_t id) {
    auto mapping = MapCmdBuffer();
    pipe_cmd_buffer_t* cmd_buffer = reinterpret_cast<pipe_cmd_buffer_t*>(mapping->ptr());
    cmd_buffer->rw_params.consumed_size = cmd_buffer->rw_params.sizes[0];
    cmd_buffer->status = 0;

    if (cmd_buffer->cmd == PIPE_CMD_CODE_WRITE) {
      // Store io buffer contents.
      auto io_buffer = MapIoBuffer();
      io_buffer_contents_.emplace_back(std::vector<uint8_t>(io_buffer_size_, 0));
      memcpy(io_buffer_contents_.back().data(), io_buffer->ptr(), io_buffer_size_);
    }

    if (cmd_buffer->cmd == PIPE_CMD_CODE_READ) {
      auto io_buffer = MapIoBuffer();
      uint32_t op = *reinterpret_cast<uint32_t*>(io_buffer->ptr());

      switch (op) {
        case kOP_rcCreateBuffer2:
        case kOP_rcCreateColorBuffer:
          *reinterpret_cast<uint32_t*>(io_buffer->ptr()) = ++buffer_id_;
          break;
        case kOP_rcMapGpaToBufferHandle2:
        case kOP_rcSetColorBufferVulkanMode2:
          *reinterpret_cast<int32_t*>(io_buffer->ptr()) = 0;
          break;
        default:
          ZX_ASSERT_MSG(false, "invalid renderControl command (op %u)", op);
      }
    }
  }

  zx_status_t GoldfishPipeGetBti(zx::bti* out_bti) {
    zx_status_t status = fake_bti_create(out_bti->reset_and_get_address());
    if (status == ZX_OK) {
      bti_ = out_bti->borrow();
    }
    return status;
  }

  zx_status_t GoldfishPipeConnectSysmem(zx::channel connection) { return ZX_OK; }

  zx_status_t GoldfishPipeRegisterSysmemHeap(uint64_t heap, zx::channel connection) {
    heap_info_[heap] = {};
    heap_info_[heap].channel = std::move(connection);
    return ZX_OK;
  }

  zx_status_t SetUpPipeDevice() {
    zx_status_t status = HandleSysmemEvents();
    if (status != ZX_OK) {
      return status;
    }

    if (!pipe_io_buffer_.is_valid()) {
      status = PrepareIoBuffer();
      if (status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }

  std::unique_ptr<VmoMapping> MapCmdBuffer() const {
    return std::make_unique<VmoMapping>(pipe_cmd_buffer_, /*size=*/sizeof(pipe_cmd_buffer_t),
                                        /*offset=*/0);
  }

  std::unique_ptr<VmoMapping> MapIoBuffer() {
    if (!pipe_io_buffer_.is_valid()) {
      PrepareIoBuffer();
    }
    return std::make_unique<VmoMapping>(pipe_io_buffer_, /*size=*/io_buffer_size_, /*offset=*/0);
  }

  bool IsPipeReady() const { return pipe_created_ && pipe_opened_; }

  uint32_t CurrentBufferHandle() { return buffer_id_; }

  const std::unordered_map<uint64_t, HeapInfo>& heap_info() const { return heap_info_; }

  const std::vector<std::vector<uint8_t>>& io_buffer_contents() const {
    return io_buffer_contents_;
  }

 private:
  class SysmemHeapEventHandler : public llcpp::fuchsia::sysmem2::Heap::SyncEventHandler {
   public:
    SysmemHeapEventHandler() = default;
    void OnRegister(llcpp::fuchsia::sysmem2::Heap::OnRegisterResponse* message) override {
      if (handler != nullptr) {
        handler(message);
      }
    }
    zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }
    void SetOnRegisterHandler(
        fit::function<void(llcpp::fuchsia::sysmem2::Heap::OnRegisterResponse*)> new_handler) {
      handler = std::move(new_handler);
    }

   private:
    fit::function<void(llcpp::fuchsia::sysmem2::Heap::OnRegisterResponse*)> handler;
  };

  zx_status_t HandleSysmemEvents() {
    zx_status_t status = ZX_OK;
    for (auto& kv : heap_info_) {
      SysmemHeapEventHandler handler;
      handler.SetOnRegisterHandler([this, heap = kv.first](
                                       llcpp::fuchsia::sysmem2::Heap::OnRegisterResponse* message) {
        auto& heap_info = heap_info_[heap];
        heap_info.is_registered = true;
        heap_info.cpu_supported = message->properties.coherency_domain_support().cpu_supported();
        heap_info.ram_supported = message->properties.coherency_domain_support().ram_supported();
        heap_info.inaccessible_supported =
            message->properties.coherency_domain_support().inaccessible_supported();
      });
      status = handler.HandleOneEvent(kv.second.channel.borrow()).status();
      if (status != ZX_OK) {
        break;
      }
    }
    return status;
  }

  zx_status_t PrepareIoBuffer() {
    uint64_t num_pinned_vmos = 0u;
    std::vector<fake_bti_pinned_vmo_info_t> pinned_vmos;
    zx_status_t status = fake_bti_get_pinned_vmos(bti_->get(), nullptr, 0, &num_pinned_vmos);
    if (status != ZX_OK) {
      return status;
    }
    if (num_pinned_vmos == 0u) {
      return ZX_ERR_NOT_FOUND;
    }

    pinned_vmos.resize(num_pinned_vmos);
    status = fake_bti_get_pinned_vmos(bti_->get(), pinned_vmos.data(), num_pinned_vmos, nullptr);
    if (status != ZX_OK) {
      return status;
    }

    pipe_io_buffer_ = zx::vmo(pinned_vmos.back().vmo);
    pinned_vmos.pop_back();
    // close all the unused handles
    for (auto vmo_info : pinned_vmos) {
      zx_handle_close(vmo_info.vmo);
    }

    status = pipe_io_buffer_.get_size(&io_buffer_size_);
    return status;
  }

  goldfish_pipe_protocol_t proto_;
  zx::unowned_bti bti_;

  static constexpr int32_t kPipeId = 1;
  zx::vmo pipe_cmd_buffer_ = zx::vmo();
  zx::vmo pipe_io_buffer_ = zx::vmo();
  size_t io_buffer_size_;

  zx::event pipe_event_;

  bool pipe_created_ = false;
  bool pipe_opened_ = false;

  int32_t buffer_id_ = 0;

  std::unordered_map<uint64_t, HeapInfo> heap_info_;
  std::vector<std::vector<uint8_t>> io_buffer_contents_;
};

class FakeAddressSpace
    : public ddk::GoldfishAddressSpaceProtocol<FakeAddressSpace, ddk::base_protocol>,
      public llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriver::Interface {
 public:
  FakeAddressSpace() : proto_({&goldfish_address_space_protocol_ops_, this}) {}

  const goldfish_address_space_protocol_t* proto() const { return &proto_; }

  zx_status_t GoldfishAddressSpaceOpenChildDriver(address_space_child_driver_type_t type,
                                                  zx::channel request) {
    return ZX_OK;
  }

  // |llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriver::Interface|
  void AllocateBlock(uint64_t size, AllocateBlockCompleter::Sync& completer) override {}
  void DeallocateBlock(uint64_t paddr, DeallocateBlockCompleter::Sync& completer) override {}
  void ClaimSharedBlock(uint64_t offset, uint64_t size,
                        ClaimSharedBlockCompleter::Sync& completer) override {}
  void UnclaimSharedBlock(uint64_t offset, UnclaimSharedBlockCompleter::Sync& completer) override {}
  void Ping(llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverPingMessage ping,
            PingCompleter::Sync& completer) override {}

 private:
  goldfish_address_space_protocol_t proto_;
  zx::channel request_;
};

class ControlDeviceTest : public testing::Test {
 public:
  void SetUp() override {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[4], 4);
    protocols[0] = {ZX_PROTOCOL_COMPOSITE,
                    *reinterpret_cast<const fake_ddk::Protocol*>(composite_.proto())};
    protocols[1] = {ZX_PROTOCOL_GOLDFISH_PIPE,
                    *reinterpret_cast<const fake_ddk::Protocol*>(pipe_.proto())};
    protocols[2] = {ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE,
                    *reinterpret_cast<const fake_ddk::Protocol*>(address_space_.proto())};
    ddk_.SetProtocols(std::move(protocols));

    dut_ = std::make_unique<Control>(fake_ddk::kFakeParent);

    ASSERT_OK(dut_->Bind());
    ASSERT_OK(pipe_.SetUpPipeDevice());
    ASSERT_TRUE(pipe_.IsPipeReady());

    fidl_client_ =
        llcpp::fuchsia::hardware::goldfish::ControlDevice::SyncClient(std::move(ddk_.FidlClient()));
  }

  void TearDown() override {
    dut_->DdkAsyncRemove();
    EXPECT_TRUE(ddk_.Ok());

    dut_.reset();
  }

 protected:
  std::unique_ptr<Control> dut_;

  FakeComposite composite_{fake_ddk::kFakeParent};
  FakePipe pipe_;
  FakeAddressSpace address_space_;

  fake_ddk::Bind ddk_;

  llcpp::fuchsia::hardware::goldfish::ControlDevice::SyncClient fidl_client_;
};

TEST_F(ControlDeviceTest, Bind) {
  const auto& heaps = pipe_.heap_info();
  ASSERT_EQ(heaps.size(), 2u);
  ASSERT_TRUE(heaps.find(static_cast<uint64_t>(
                  llcpp::fuchsia::sysmem2::HeapType::GOLDFISH_DEVICE_LOCAL)) != heaps.end());
  ASSERT_TRUE(heaps.find(static_cast<uint64_t>(
                  llcpp::fuchsia::sysmem2::HeapType::GOLDFISH_HOST_VISIBLE)) != heaps.end());

  const auto& device_local_heap_info =
      heaps.at(static_cast<uint64_t>(llcpp::fuchsia::sysmem2::HeapType::GOLDFISH_DEVICE_LOCAL));
  EXPECT_TRUE(device_local_heap_info.channel.is_valid());
  EXPECT_TRUE(device_local_heap_info.is_registered);
  EXPECT_TRUE(device_local_heap_info.inaccessible_supported);

  const auto& host_visible_heap_info =
      heaps.at(static_cast<uint64_t>(llcpp::fuchsia::sysmem2::HeapType::GOLDFISH_HOST_VISIBLE));
  EXPECT_TRUE(host_visible_heap_info.channel.is_valid());
  EXPECT_TRUE(host_visible_heap_info.is_registered);
  EXPECT_TRUE(host_visible_heap_info.cpu_supported);
}

// Test |fuchsia.hardware.goldfish.Control.CreateBuffer2| method.
class BufferTest : public ControlDeviceTest, public testing::WithParamInterface<uint32_t> {};

TEST_P(BufferTest, TestCreate2) {
  constexpr size_t kSize = 65536u;
  constexpr uint64_t kPhysicalAddress = 0x12345678abcd0000;
  const auto memory_property = GetParam();
  const bool is_host_visible =
      memory_property == llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE;

  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  dut_->RegisterBufferHandle(buffer_vmo);
  auto create_params_builder =
      llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Frame>())
          .set_size(std::make_unique<uint64_t>(kSize))
          .set_memory_property(std::make_unique<uint32_t>(memory_property));
  if (is_host_visible) {
    create_params_builder.set_physical_address(std::make_unique<uint64_t>(kPhysicalAddress));
  }
  auto create_params = create_params_builder.build();
  auto create_buffer_result =
      fidl_client_.CreateBuffer2(std::move(buffer_vmo), std::move(create_params));

  ASSERT_TRUE(create_buffer_result.ok());
  ASSERT_TRUE(create_buffer_result.value().result.is_response());

  CreateBuffer2Cmd create_buffer_cmd{
      .op = kOP_rcCreateBuffer2,
      .size = kSize_rcCreateBuffer2,
      .buffer_size = kSize,
      .memory_property = memory_property,
  };

  MapGpaToBufferHandle2Cmd map_gpa_cmd{
      .op = kOP_rcMapGpaToBufferHandle2,
      .size = kSize_rcMapGpaToBufferHandle2,
      .id = pipe_.CurrentBufferHandle(),
      .gpa = kPhysicalAddress,
      .map_size = kSize,
  };

  const auto& io_buffer_contents = pipe_.io_buffer_contents();
  size_t create_buffer_cmd_idx = 0;
  if (is_host_visible) {
    ASSERT_GE(io_buffer_contents.size(), 2u);
    create_buffer_cmd_idx = io_buffer_contents.size() - 2;
  } else {
    ASSERT_GE(io_buffer_contents.size(), 1u);
    create_buffer_cmd_idx = io_buffer_contents.size() - 1;
  }

  EXPECT_EQ(memcmp(&create_buffer_cmd, io_buffer_contents[create_buffer_cmd_idx].data(),
                   sizeof(CreateBuffer2Cmd)),
            0);
  if (is_host_visible) {
    EXPECT_EQ(memcmp(&map_gpa_cmd, io_buffer_contents[create_buffer_cmd_idx + 1].data(),
                     sizeof(MapGpaToBufferHandle2Cmd)),
              0);
  }
}

INSTANTIATE_TEST_SUITE_P(
    ControlDeviceTest, BufferTest,
    testing::Values(llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL,
                    llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE),
    [](const testing::TestParamInfo<BufferTest::ParamType>& info) {
      std::string memory_property;
      switch (info.param) {
        case llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL:
          memory_property = "DEVICE_LOCAL";
          break;
        case llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE:
          memory_property = "HOST_VISIBLE";
          break;
        default:
          memory_property = "UNSUPPORTED_MEMORY_PROPERTY";
      }
      return memory_property;
    });

TEST_F(ControlDeviceTest, CreateBuffer2_AlreadyExists) {
  constexpr size_t kSize = 65536u;
  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  zx::vmo copy_vmo;
  ASSERT_OK(buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &copy_vmo));

  dut_->RegisterBufferHandle(buffer_vmo);
  auto create_params =
      llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Frame>())
          .set_size(std::make_unique<uint64_t>(kSize))
          .set_memory_property(std::make_unique<uint32_t>(
              llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
          .build();
  auto create_buffer_result =
      fidl_client_.CreateBuffer2(std::move(buffer_vmo), std::move(create_params));

  ASSERT_TRUE(create_buffer_result.ok());
  ASSERT_TRUE(create_buffer_result.value().result.is_response());

  auto create_params2 =
      llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Frame>())
          .set_size(std::make_unique<uint64_t>(kSize))
          .set_memory_property(std::make_unique<uint32_t>(
              llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
          .build();
  auto create_copy_buffer_result =
      fidl_client_.CreateBuffer2(std::move(copy_vmo), std::move(create_params2));

  ASSERT_TRUE(create_copy_buffer_result.ok());
  ASSERT_TRUE(create_copy_buffer_result.value().result.is_err());
  ASSERT_EQ(create_copy_buffer_result.value().result.err(), ZX_ERR_ALREADY_EXISTS);
}

TEST_F(ControlDeviceTest, CreateBuffer2_InvalidArgs) {
  constexpr size_t kSize = 65536u;
  {
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Frame>())
            // missing size
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();
    auto result = fidl_client_.CreateBuffer2(std::move(buffer_vmo), std::move(create_params));
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value().result.is_err());
    ASSERT_EQ(result.value().result.err(), ZX_ERR_INVALID_ARGS);

    dut_->FreeBufferHandle(info.koid);
  }

  {
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);
    auto create_params2 =
        llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Frame>())
            .set_size(std::make_unique<uint64_t>(kSize))
            // missing memory property
            .build();
    auto result = fidl_client_.CreateBuffer2(std::move(buffer_vmo), std::move(create_params2));
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value().result.is_err());
    ASSERT_EQ(result.value().result.err(), ZX_ERR_INVALID_ARGS);

    dut_->FreeBufferHandle(info.koid);
  }
}

TEST_F(ControlDeviceTest, CreateBuffer2_InvalidVmo) {
  constexpr size_t kSize = 65536u;
  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  auto create_params =
      llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Frame>())
          .set_size(std::make_unique<uint64_t>(kSize))
          .set_memory_property(std::make_unique<uint32_t>(
              llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
          .build();
  auto create_unregistered_buffer_result =
      fidl_client_.CreateBuffer2(std::move(buffer_vmo), std::move(create_params));

  ASSERT_TRUE(create_unregistered_buffer_result.ok());
  ASSERT_TRUE(create_unregistered_buffer_result.value().result.is_err());
  ASSERT_EQ(create_unregistered_buffer_result.value().result.err(), ZX_ERR_INVALID_ARGS);

  auto create_params2 =
      llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Frame>())
          .set_size(std::make_unique<uint64_t>(kSize))
          .set_memory_property(std::make_unique<uint32_t>(
              llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
          .build();
  auto create_invalid_buffer_result =
      fidl_client_.CreateBuffer2(zx::vmo(), std::move(create_params2));

  ASSERT_EQ(create_invalid_buffer_result.status(), ZX_ERR_INVALID_ARGS);
}

// Test |fuchsia.hardware.goldfish.Control.CreateColorBuffer2| method.
class ColorBufferTest
    : public ControlDeviceTest,
      public testing::WithParamInterface<
          std::tuple<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType, uint32_t>> {};

TEST_P(ColorBufferTest, TestCreate) {
  constexpr uint32_t kWidth = 1024u;
  constexpr uint32_t kHeight = 768u;
  constexpr uint32_t kSize = kWidth * kHeight * 4;
  constexpr uint64_t kPhysicalAddress = 0x12345678abcd0000;
  const auto format = std::get<0>(GetParam());
  const auto memory_property = std::get<1>(GetParam());
  const bool is_host_visible =
      memory_property == llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE;

  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  dut_->RegisterBufferHandle(buffer_vmo);
  auto create_params_builder =
      llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
          .set_width(std::make_unique<uint32_t>(kWidth))
          .set_height(std::make_unique<uint32_t>(kHeight))
          .set_format(
              std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(format))
          .set_memory_property(std::make_unique<uint32_t>(memory_property));
  if (is_host_visible) {
    create_params_builder.set_physical_address(std::make_unique<uint64_t>(kPhysicalAddress));
  }
  auto create_params = create_params_builder.build();
  auto create_color_buffer_result =
      fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

  ASSERT_TRUE(create_color_buffer_result.ok());
  EXPECT_OK(create_color_buffer_result.value().res);
  const int32_t expected_page_offset = is_host_visible ? 0 : -1;
  EXPECT_EQ(create_color_buffer_result.value().hw_address_page_offset, expected_page_offset);

  CreateColorBufferCmd create_color_buffer_cmd{
      .op = kOP_rcCreateColorBuffer,
      .size = kSize_rcCreateColorBuffer,
      .width = kWidth,
      .height = kHeight,
      .internalformat = static_cast<uint32_t>(format),
  };

  SetColorBufferVulkanMode2Cmd set_vulkan_mode_cmd{
      .op = kOP_rcSetColorBufferVulkanMode2,
      .size = kSize_rcSetColorBufferVulkanMode2,
      .id = pipe_.CurrentBufferHandle(),
      .mode = 1u,  // VULKAN_ONLY
      .memory_property = memory_property,
  };

  MapGpaToBufferHandle2Cmd map_gpa_cmd{
      .op = kOP_rcMapGpaToBufferHandle2,
      .size = kSize_rcMapGpaToBufferHandle2,
      .id = pipe_.CurrentBufferHandle(),
      .gpa = kPhysicalAddress,
      .map_size = kSize,
  };

  const auto& io_buffer_contents = pipe_.io_buffer_contents();
  size_t create_color_buffer_cmd_idx = 0;
  if (is_host_visible) {
    ASSERT_GE(io_buffer_contents.size(), 3u);
    create_color_buffer_cmd_idx = io_buffer_contents.size() - 3;
  } else {
    ASSERT_GE(io_buffer_contents.size(), 2u);
    create_color_buffer_cmd_idx = io_buffer_contents.size() - 2;
  }

  EXPECT_EQ(memcmp(&create_color_buffer_cmd, io_buffer_contents[create_color_buffer_cmd_idx].data(),
                   sizeof(CreateColorBufferCmd)),
            0);
  EXPECT_EQ(memcmp(&set_vulkan_mode_cmd, io_buffer_contents[create_color_buffer_cmd_idx + 1].data(),
                   sizeof(set_vulkan_mode_cmd)),
            0);
  if (is_host_visible) {
    EXPECT_EQ(memcmp(&map_gpa_cmd, io_buffer_contents[create_color_buffer_cmd_idx + 2].data(),
                     sizeof(MapGpaToBufferHandle2Cmd)),
              0);
  }
}

INSTANTIATE_TEST_SUITE_P(
    ControlDeviceTest, ColorBufferTest,
    testing::Combine(
        testing::Values(llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::RG,
                        llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::RGBA,
                        llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA,
                        llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::LUMINANCE),
        testing::Values(llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL,
                        llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE)),
    [](const testing::TestParamInfo<ColorBufferTest::ParamType>& info) {
      std::string format;
      switch (std::get<0>(info.param)) {
        case llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::RG:
          format = "RG";
          break;
        case llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::RGBA:
          format = "RGBA";
          break;
        case llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::BGRA:
          format = "BGRA";
          break;
        case llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::LUMINANCE:
          format = "LUMINANCE";
          break;
        default:
          format = "UNSUPPORTED_FORMAT";
      }

      std::string memory_property;
      switch (std::get<1>(info.param)) {
        case llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL:
          memory_property = "DEVICE_LOCAL";
          break;
        case llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE:
          memory_property = "HOST_VISIBLE";
          break;
        default:
          memory_property = "UNSUPPORTED_MEMORY_PROPERTY";
      }

      return format + "_" + memory_property;
    });

TEST_F(ControlDeviceTest, CreateColorBuffer2_AlreadyExists) {
  constexpr uint32_t kWidth = 1024u;
  constexpr uint32_t kHeight = 768u;
  constexpr uint32_t kSize = kWidth * kHeight * 4;
  constexpr auto kFormat = llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::RGBA;
  constexpr auto kMemoryProperty = llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL;

  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  zx::vmo copy_vmo;
  ASSERT_OK(buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &copy_vmo));

  dut_->RegisterBufferHandle(buffer_vmo);
  auto create_params =
      llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
          .set_width(std::make_unique<uint32_t>(kWidth))
          .set_height(std::make_unique<uint32_t>(kHeight))
          .set_format(
              std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(kFormat))
          .set_memory_property(std::make_unique<uint32_t>(kMemoryProperty))
          .build();
  auto create_color_buffer_result =
      fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

  ASSERT_TRUE(create_color_buffer_result.ok());
  EXPECT_OK(create_color_buffer_result.value().res);

  create_params =
      llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
          .set_width(std::make_unique<uint32_t>(kWidth))
          .set_height(std::make_unique<uint32_t>(kHeight))
          .set_format(
              std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(kFormat))
          .set_memory_property(std::make_unique<uint32_t>(kMemoryProperty))
          .build();
  auto create_copy_buffer_result =
      fidl_client_.CreateColorBuffer2(std::move(copy_vmo), std::move(create_params));

  ASSERT_TRUE(create_copy_buffer_result.ok());
  ASSERT_EQ(create_copy_buffer_result.value().res, ZX_ERR_ALREADY_EXISTS);
}

TEST_F(ControlDeviceTest, CreateColorBuffer2_InvalidArgs) {
  constexpr uint32_t kWidth = 1024u;
  constexpr uint32_t kHeight = 768u;
  constexpr uint32_t kSize = kWidth * kHeight * 4;
  constexpr auto kFormat = llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::RGBA;
  constexpr auto kMemoryProperty = llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL;

  {
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            // missing width
            .set_height(std::make_unique<uint32_t>(kHeight))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                kFormat))
            .set_memory_property(std::make_unique<uint32_t>(kMemoryProperty))
            .build();
    auto create_color_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

    ASSERT_TRUE(create_color_buffer_result.ok());
    EXPECT_EQ(create_color_buffer_result.value().res, ZX_ERR_INVALID_ARGS);

    dut_->FreeBufferHandle(info.koid);
  }

  {
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(kWidth))
            // missing height
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                kFormat))
            .set_memory_property(std::make_unique<uint32_t>(kMemoryProperty))
            .build();
    auto create_color_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

    ASSERT_TRUE(create_color_buffer_result.ok());
    EXPECT_EQ(create_color_buffer_result.value().res, ZX_ERR_INVALID_ARGS);

    dut_->FreeBufferHandle(info.koid);
  }

  {
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(kWidth))
            .set_height(std::make_unique<uint32_t>(kHeight))
            // missing format
            .set_memory_property(std::make_unique<uint32_t>(kMemoryProperty))
            .build();
    auto create_color_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

    ASSERT_TRUE(create_color_buffer_result.ok());
    EXPECT_EQ(create_color_buffer_result.value().res, ZX_ERR_INVALID_ARGS);

    dut_->FreeBufferHandle(info.koid);
  }

  {
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(kWidth))
            .set_height(std::make_unique<uint32_t>(kHeight))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                kFormat))
            // missing memory property
            .build();
    auto create_color_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

    ASSERT_TRUE(create_color_buffer_result.ok());
    EXPECT_EQ(create_color_buffer_result.value().res, ZX_ERR_INVALID_ARGS);

    dut_->FreeBufferHandle(info.koid);
  }

  {
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(kWidth))
            .set_height(std::make_unique<uint32_t>(kHeight))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                kFormat))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE))
            // missing physical address
            .build();
    auto create_color_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

    ASSERT_TRUE(create_color_buffer_result.ok());
    EXPECT_EQ(create_color_buffer_result.value().res, ZX_ERR_INVALID_ARGS);

    dut_->FreeBufferHandle(info.koid);
  }
}

TEST_F(ControlDeviceTest, CreateColorBuffer2_InvalidVmo) {
  constexpr uint32_t kWidth = 1024u;
  constexpr uint32_t kHeight = 768u;
  constexpr uint32_t kSize = kWidth * kHeight * 4;
  constexpr auto kFormat = llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::RGBA;
  constexpr auto kMemoryProperty = llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL;

  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  auto create_params =
      llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
          .set_width(std::make_unique<uint32_t>(kWidth))
          .set_height(std::make_unique<uint32_t>(kHeight))
          .set_format(
              std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(kFormat))
          .set_memory_property(std::make_unique<uint32_t>(kMemoryProperty))
          .build();
  auto create_unregistered_buffer_result =
      fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

  ASSERT_TRUE(create_unregistered_buffer_result.ok());
  EXPECT_EQ(create_unregistered_buffer_result.value().res, ZX_ERR_INVALID_ARGS);

  create_params =
      llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
          std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
          .set_width(std::make_unique<uint32_t>(kWidth))
          .set_height(std::make_unique<uint32_t>(kHeight))
          .set_format(
              std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(kFormat))
          .set_memory_property(std::make_unique<uint32_t>(kMemoryProperty))
          .build();
  auto create_invalid_buffer_result =
      fidl_client_.CreateColorBuffer2(zx::vmo(), std::move(create_params));

  ASSERT_EQ(create_invalid_buffer_result.status(), ZX_ERR_INVALID_ARGS);
}

// Test |fuchsia.hardware.goldfish.Control.GetBufferHandle| method.
TEST_F(ControlDeviceTest, GetBufferHandle_Success) {
  zx::vmo buffer_vmo;
  zx::vmo color_buffer_vmo;

  // Create data buffer.
  {
    constexpr size_t kSize = 65536u;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx::vmo copy_vmo;
    ASSERT_OK(buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &copy_vmo));

    dut_->RegisterBufferHandle(buffer_vmo);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateBuffer2Params::Frame>())
            .set_size(std::make_unique<uint64_t>(kSize))
            .set_memory_property(std::make_unique<uint32_t>(
                llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL))
            .build();
    auto create_buffer_result =
        fidl_client_.CreateBuffer2(std::move(copy_vmo), std::move(create_params));

    ASSERT_TRUE(create_buffer_result.ok());
    EXPECT_TRUE(create_buffer_result.value().result.is_response());
  }

  // Create color buffer.
  {
    constexpr uint32_t kWidth = 1024u;
    constexpr uint32_t kHeight = 768u;
    constexpr uint32_t kSize = kWidth * kHeight * 4;
    constexpr auto kFormat = llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType::RGBA;
    constexpr auto kMemoryProperty =
        llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_DEVICE_LOCAL;

    ASSERT_OK(zx::vmo::create(kSize, 0u, &color_buffer_vmo));

    zx::vmo copy_vmo;
    ASSERT_OK(color_buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &copy_vmo));

    dut_->RegisterBufferHandle(color_buffer_vmo);
    auto create_params =
        llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Builder(
            std::make_unique<llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params::Frame>())
            .set_width(std::make_unique<uint32_t>(kWidth))
            .set_height(std::make_unique<uint32_t>(kHeight))
            .set_format(std::make_unique<llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType>(
                kFormat))
            .set_memory_property(std::make_unique<uint32_t>(kMemoryProperty))
            .build();
    auto create_color_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(copy_vmo), std::move(create_params));

    ASSERT_TRUE(create_color_buffer_result.ok());
    EXPECT_OK(create_color_buffer_result.value().res);
  }

  auto get_buffer_handle_result = fidl_client_.GetBufferHandle(std::move(buffer_vmo));
  ASSERT_TRUE(get_buffer_handle_result.ok());
  EXPECT_OK(get_buffer_handle_result.value().res);
  EXPECT_NE(get_buffer_handle_result.value().id, 0u);
  EXPECT_EQ(get_buffer_handle_result.value().type,
            llcpp::fuchsia::hardware::goldfish::BufferHandleType::BUFFER);

  auto get_color_buffer_handle_result = fidl_client_.GetBufferHandle(std::move(color_buffer_vmo));
  ASSERT_TRUE(get_color_buffer_handle_result.ok());
  EXPECT_OK(get_color_buffer_handle_result.value().res);
  EXPECT_NE(get_color_buffer_handle_result.value().id, 0u);
  EXPECT_NE(get_color_buffer_handle_result.value().id, get_buffer_handle_result.value().id);
  EXPECT_EQ(get_color_buffer_handle_result.value().type,
            llcpp::fuchsia::hardware::goldfish::BufferHandleType::COLOR_BUFFER);
}

TEST_F(ControlDeviceTest, GetBufferHandle_Invalid) {
  // Register data buffer, but don't create it.
  {
    constexpr size_t kSize = 65536u;
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);

    auto get_buffer_handle_result = fidl_client_.GetBufferHandle(std::move(buffer_vmo));
    ASSERT_TRUE(get_buffer_handle_result.ok());
    EXPECT_EQ(get_buffer_handle_result.value().res, ZX_ERR_NOT_FOUND);

    dut_->FreeBufferHandle(info.koid);
  }

  // Check non-registered buffer VMO.
  {
    constexpr size_t kSize = 65536u;
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    auto get_buffer_handle_result = fidl_client_.GetBufferHandle(std::move(buffer_vmo));
    ASSERT_TRUE(get_buffer_handle_result.ok());
    EXPECT_EQ(get_buffer_handle_result.value().res, ZX_ERR_INVALID_ARGS);
  }

  // Check invalid buffer VMO.
  {
    auto get_buffer_handle_result = fidl_client_.GetBufferHandle(zx::vmo());
    ASSERT_EQ(get_buffer_handle_result.status(), ZX_ERR_INVALID_ARGS);
  }
}

}  // namespace
}  // namespace goldfish
