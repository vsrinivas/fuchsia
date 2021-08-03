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

// TODO(fxbug.dev/80642): Use //src/devices/lib/goldfish/fake_pipe instead.
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
  class SysmemHeapEventHandler : public fidl::WireSyncEventHandler<fuchsia_sysmem2::Heap> {
   public:
    SysmemHeapEventHandler() = default;
    void OnRegister(fidl::WireResponse<fuchsia_sysmem2::Heap::OnRegister>* message) override {
      if (handler != nullptr) {
        handler(message);
      }
    }
    zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }
    void SetOnRegisterHandler(
        fit::function<void(fidl::WireResponse<fuchsia_sysmem2::Heap::OnRegister>*)> new_handler) {
      handler = std::move(new_handler);
    }

   private:
    fit::function<void(fidl::WireResponse<fuchsia_sysmem2::Heap::OnRegister>*)> handler;
  };

  zx_status_t HandleSysmemEvents() {
    zx_status_t status = ZX_OK;
    for (auto& kv : heap_info_) {
      SysmemHeapEventHandler handler;
      handler.SetOnRegisterHandler(
          [this, heap = kv.first](fidl::WireResponse<fuchsia_sysmem2::Heap::OnRegister>* message) {
            auto& heap_info = heap_info_[heap];
            heap_info.is_registered = true;
            heap_info.cpu_supported =
                message->properties.coherency_domain_support().cpu_supported();
            heap_info.ram_supported =
                message->properties.coherency_domain_support().ram_supported();
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
      public fidl::WireServer<fuchsia_hardware_goldfish::AddressSpaceChildDriver> {
 public:
  FakeAddressSpace() : proto_({&goldfish_address_space_protocol_ops_, this}) {}

  const goldfish_address_space_protocol_t* proto() const { return &proto_; }

  zx_status_t GoldfishAddressSpaceOpenChildDriver(address_space_child_driver_type_t type,
                                                  zx::channel request) {
    return ZX_OK;
  }

  // |fidl::WireServer<fuchsia_hardware_goldfish::AddressSpaceChildDriver>|
  void AllocateBlock(AllocateBlockRequestView request,
                     AllocateBlockCompleter::Sync& completer) override {}
  void DeallocateBlock(DeallocateBlockRequestView request,
                       DeallocateBlockCompleter::Sync& completer) override {}
  void ClaimSharedBlock(ClaimSharedBlockRequestView request,
                        ClaimSharedBlockCompleter::Sync& completer) override {}
  void UnclaimSharedBlock(UnclaimSharedBlockRequestView request,
                          UnclaimSharedBlockCompleter::Sync& completer) override {}
  void Ping(PingRequestView request, PingCompleter::Sync& completer) override {}

 private:
  goldfish_address_space_protocol_t proto_;
  zx::channel request_;
};

class FakeSync : public ddk::GoldfishSyncProtocol<FakeSync, ddk::base_protocol> {
 public:
  FakeSync() : proto_({&goldfish_sync_protocol_ops_, this}) {}

  const goldfish_sync_protocol_t* proto() const { return &proto_; }

  zx_status_t GoldfishSyncCreateTimeline(zx::channel request) { return ZX_OK; }

 private:
  goldfish_sync_protocol_t proto_;
  zx::channel request_;
};

class ControlDeviceTest : public testing::Test {
 public:
  void SetUp() override {
    fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[3], 3);
    fragments[0].name = "goldfish-pipe";
    fragments[0].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_GOLDFISH_PIPE, *reinterpret_cast<const fake_ddk::Protocol*>(pipe_.proto())});
    fragments[1].name = "goldfish-address-space";
    fragments[1].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE,
        *reinterpret_cast<const fake_ddk::Protocol*>(address_space_.proto())});
    fragments[2].name = "goldfish-sync";
    fragments[2].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_GOLDFISH_SYNC, *reinterpret_cast<const fake_ddk::Protocol*>(sync_.proto())});
    ddk_.SetFragments(std::move(fragments));

    dut_ = std::make_unique<Control>(fake_ddk::kFakeParent);

    ASSERT_OK(dut_->Bind());
    ASSERT_OK(pipe_.SetUpPipeDevice());
    ASSERT_TRUE(pipe_.IsPipeReady());

    fidl_client_ = fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice>(
        std::move(ddk_.FidlClient()));
  }

  void TearDown() override {
    dut_->DdkAsyncRemove();
    EXPECT_TRUE(ddk_.Ok());

    dut_.reset();
  }

 protected:
  std::unique_ptr<Control> dut_;

  FakePipe pipe_;
  FakeAddressSpace address_space_;
  FakeSync sync_;

  fake_ddk::Bind ddk_;

  fidl::WireSyncClient<fuchsia_hardware_goldfish::ControlDevice> fidl_client_;
};

TEST_F(ControlDeviceTest, Bind) {
  const auto& heaps = pipe_.heap_info();
  ASSERT_EQ(heaps.size(), 2u);
  ASSERT_TRUE(heaps.find(static_cast<uint64_t>(
                  fuchsia_sysmem2::wire::HeapType::kGoldfishDeviceLocal)) != heaps.end());
  ASSERT_TRUE(heaps.find(static_cast<uint64_t>(
                  fuchsia_sysmem2::wire::HeapType::kGoldfishHostVisible)) != heaps.end());

  const auto& device_local_heap_info =
      heaps.at(static_cast<uint64_t>(fuchsia_sysmem2::wire::HeapType::kGoldfishDeviceLocal));
  EXPECT_TRUE(device_local_heap_info.channel.is_valid());
  EXPECT_TRUE(device_local_heap_info.is_registered);
  EXPECT_TRUE(device_local_heap_info.inaccessible_supported);

  const auto& host_visible_heap_info =
      heaps.at(static_cast<uint64_t>(fuchsia_sysmem2::wire::HeapType::kGoldfishHostVisible));
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
      memory_property == fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible;

  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  dut_->RegisterBufferHandle(buffer_vmo);
  fidl::Arena allocator;
  fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
  create_params.set_size(allocator, kSize).set_memory_property(allocator, memory_property);
  if (is_host_visible) {
    create_params.set_physical_address(allocator, kPhysicalAddress);
  }
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
    testing::Values(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal,
                    fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible),
    [](const testing::TestParamInfo<BufferTest::ParamType>& info) {
      std::string memory_property;
      switch (info.param) {
        case fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal:
          memory_property = "DEVICE_LOCAL";
          break;
        case fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible:
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
  fidl::Arena allocator;
  fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
  create_params.set_size(allocator, kSize)
      .set_memory_property(allocator, fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);
  auto create_buffer_result =
      fidl_client_.CreateBuffer2(std::move(buffer_vmo), std::move(create_params));

  ASSERT_TRUE(create_buffer_result.ok());
  ASSERT_TRUE(create_buffer_result.value().result.is_response());

  fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params2(allocator);
  create_params2.set_size(allocator, kSize)
      .set_memory_property(allocator, fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);
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

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
    // missing size
    create_params.set_memory_property(allocator,
                                      fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

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

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params2(allocator);
    // missing memory property
    create_params2.set_size(allocator, kSize);

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

  fidl::Arena allocator;
  fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
  create_params.set_size(allocator, kSize)
      .set_memory_property(allocator, fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

  auto create_unregistered_buffer_result =
      fidl_client_.CreateBuffer2(std::move(buffer_vmo), std::move(create_params));

  ASSERT_TRUE(create_unregistered_buffer_result.ok());
  ASSERT_TRUE(create_unregistered_buffer_result.value().result.is_err());
  ASSERT_EQ(create_unregistered_buffer_result.value().result.err(), ZX_ERR_INVALID_ARGS);

  fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params2(allocator);
  create_params2.set_size(allocator, kSize)
      .set_memory_property(allocator, fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

  auto create_invalid_buffer_result =
      fidl_client_.CreateBuffer2(zx::vmo(), std::move(create_params2));

  ASSERT_EQ(create_invalid_buffer_result.status(), ZX_ERR_INVALID_ARGS);
}

// Test |fuchsia.hardware.goldfish.Control.CreateColorBuffer2| method.
class ColorBufferTest
    : public ControlDeviceTest,
      public testing::WithParamInterface<
          std::tuple<fuchsia_hardware_goldfish::wire::ColorBufferFormatType, uint32_t>> {};

TEST_P(ColorBufferTest, TestCreate) {
  constexpr uint32_t kWidth = 1024u;
  constexpr uint32_t kHeight = 768u;
  constexpr uint32_t kSize = kWidth * kHeight * 4;
  constexpr uint64_t kPhysicalAddress = 0x12345678abcd0000;
  const auto format = std::get<0>(GetParam());
  const auto memory_property = std::get<1>(GetParam());
  const bool is_host_visible =
      memory_property == fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible;

  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  dut_->RegisterBufferHandle(buffer_vmo);

  fidl::Arena allocator;
  fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
  create_params.set_width(allocator, kWidth)
      .set_height(allocator, kHeight)
      .set_format(allocator, format)
      .set_memory_property(allocator, memory_property);
  if (is_host_visible) {
    create_params.set_physical_address(allocator, kPhysicalAddress);
  }

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
        testing::Values(fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRg,
                        fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRgba,
                        fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra,
                        fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kLuminance),
        testing::Values(fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal,
                        fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible)),
    [](const testing::TestParamInfo<ColorBufferTest::ParamType>& info) {
      std::string format;
      switch (std::get<0>(info.param)) {
        case fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRg:
          format = "RG";
          break;
        case fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRgba:
          format = "RGBA";
          break;
        case fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kBgra:
          format = "BGRA";
          break;
        case fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kLuminance:
          format = "LUMINANCE";
          break;
        default:
          format = "UNSUPPORTED_FORMAT";
      }

      std::string memory_property;
      switch (std::get<1>(info.param)) {
        case fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal:
          memory_property = "DEVICE_LOCAL";
          break;
        case fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible:
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
  constexpr auto kFormat = fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRgba;
  constexpr auto kMemoryProperty = fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal;

  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  zx::vmo copy_vmo;
  ASSERT_OK(buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &copy_vmo));

  dut_->RegisterBufferHandle(buffer_vmo);

  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(allocator, kWidth)
        .set_height(allocator, kHeight)
        .set_format(allocator, kFormat)
        .set_memory_property(allocator, kMemoryProperty);

    auto create_color_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

    ASSERT_TRUE(create_color_buffer_result.ok());
    EXPECT_OK(create_color_buffer_result.value().res);
  }

  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(allocator, kWidth)
        .set_height(allocator, kHeight)
        .set_format(allocator, kFormat)
        .set_memory_property(allocator, kMemoryProperty);

    auto create_copy_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(copy_vmo), std::move(create_params));

    ASSERT_TRUE(create_copy_buffer_result.ok());
    ASSERT_EQ(create_copy_buffer_result.value().res, ZX_ERR_ALREADY_EXISTS);
  }
}

TEST_F(ControlDeviceTest, CreateColorBuffer2_InvalidArgs) {
  constexpr uint32_t kWidth = 1024u;
  constexpr uint32_t kHeight = 768u;
  constexpr uint32_t kSize = kWidth * kHeight * 4;
  constexpr auto kFormat = fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRgba;
  constexpr auto kMemoryProperty = fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal;

  {
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // missing width
    create_params.set_height(allocator, kHeight)
        .set_format(allocator, kFormat)
        .set_memory_property(allocator, kMemoryProperty);

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

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // missing height
    create_params.set_width(allocator, kWidth)
        .set_format(allocator, kFormat)
        .set_memory_property(allocator, kMemoryProperty);

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

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // missing format
    create_params.set_width(allocator, kWidth)
        .set_height(allocator, kHeight)
        .set_memory_property(allocator, kMemoryProperty);

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

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // missing memory property
    create_params.set_width(allocator, kWidth)
        .set_height(allocator, kHeight)
        .set_format(allocator, kFormat);

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

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    // missing physical address
    create_params.set_width(allocator, kWidth)
        .set_height(allocator, kHeight)
        .set_format(allocator, kFormat)
        .set_memory_property(allocator,
                             fuchsia_hardware_goldfish::wire::kMemoryPropertyHostVisible);

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
  constexpr auto kFormat = fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRgba;
  constexpr auto kMemoryProperty = fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal;

  zx::vmo buffer_vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(allocator, kWidth)
        .set_height(allocator, kHeight)
        .set_format(allocator, kFormat)
        .set_memory_property(allocator, kMemoryProperty);

    auto create_unregistered_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(buffer_vmo), std::move(create_params));

    ASSERT_TRUE(create_unregistered_buffer_result.ok());
    EXPECT_EQ(create_unregistered_buffer_result.value().res, ZX_ERR_INVALID_ARGS);
  }

  {
    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(allocator, kWidth)
        .set_height(allocator, kHeight)
        .set_format(allocator, kFormat)
        .set_memory_property(allocator, kMemoryProperty);

    auto create_invalid_buffer_result =
        fidl_client_.CreateColorBuffer2(zx::vmo(), std::move(create_params));

    ASSERT_EQ(create_invalid_buffer_result.status(), ZX_ERR_INVALID_ARGS);
  }
}

// Test |fuchsia.hardware.goldfish.Control.GetBufferHandle| method.
TEST_F(ControlDeviceTest, GetBufferHandle_Success) {
  zx::vmo buffer_vmo, buffer_vmo_dup;
  zx::vmo color_buffer_vmo, color_buffer_vmo_dup;

  // Create data buffer.
  {
    constexpr size_t kSize = 65536u;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));
    ASSERT_OK(buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &buffer_vmo_dup));

    zx::vmo copy_vmo;
    ASSERT_OK(buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &copy_vmo));

    dut_->RegisterBufferHandle(buffer_vmo);

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params(allocator);
    create_params.set_size(allocator, kSize)
        .set_memory_property(allocator,
                             fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

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
    constexpr auto kFormat = fuchsia_hardware_goldfish::wire::ColorBufferFormatType::kRgba;
    constexpr auto kMemoryProperty = fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal;

    ASSERT_OK(zx::vmo::create(kSize, 0u, &color_buffer_vmo));
    ASSERT_OK(color_buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &color_buffer_vmo_dup));

    zx::vmo copy_vmo;
    ASSERT_OK(color_buffer_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &copy_vmo));

    dut_->RegisterBufferHandle(color_buffer_vmo);

    fidl::Arena allocator;
    fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params(allocator);
    create_params.set_width(allocator, kWidth)
        .set_height(allocator, kHeight)
        .set_format(allocator, kFormat)
        .set_memory_property(allocator, kMemoryProperty);

    auto create_color_buffer_result =
        fidl_client_.CreateColorBuffer2(std::move(copy_vmo), std::move(create_params));

    ASSERT_TRUE(create_color_buffer_result.ok());
    EXPECT_OK(create_color_buffer_result.value().res);
  }

  // Test GetBufferHandle() method.
  auto get_buffer_handle_result = fidl_client_.GetBufferHandle(std::move(buffer_vmo));
  ASSERT_TRUE(get_buffer_handle_result.ok());
  EXPECT_OK(get_buffer_handle_result.value().res);
  EXPECT_NE(get_buffer_handle_result.value().id, 0u);
  EXPECT_EQ(get_buffer_handle_result.value().type,
            fuchsia_hardware_goldfish::wire::BufferHandleType::kBuffer);

  auto get_color_buffer_handle_result = fidl_client_.GetBufferHandle(std::move(color_buffer_vmo));
  ASSERT_TRUE(get_color_buffer_handle_result.ok());
  EXPECT_OK(get_color_buffer_handle_result.value().res);
  EXPECT_NE(get_color_buffer_handle_result.value().id, 0u);
  EXPECT_NE(get_color_buffer_handle_result.value().id, get_buffer_handle_result.value().id);
  EXPECT_EQ(get_color_buffer_handle_result.value().type,
            fuchsia_hardware_goldfish::wire::BufferHandleType::kColorBuffer);

  // Test GetBufferHandleInfo() method.
  auto get_buffer_handle_info_result = fidl_client_.GetBufferHandleInfo(std::move(buffer_vmo_dup));
  ASSERT_TRUE(get_buffer_handle_info_result.ok());
  ASSERT_TRUE(get_buffer_handle_info_result.value().result.is_response());

  const auto& buffer_handle_info = get_buffer_handle_info_result.value().result.response().info;
  EXPECT_NE(buffer_handle_info.id(), 0u);
  EXPECT_EQ(buffer_handle_info.type(), fuchsia_hardware_goldfish::wire::BufferHandleType::kBuffer);
  EXPECT_EQ(buffer_handle_info.memory_property(),
            fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);

  auto get_color_buffer_handle_info_result =
      fidl_client_.GetBufferHandleInfo(std::move(color_buffer_vmo_dup));
  ASSERT_TRUE(get_color_buffer_handle_info_result.ok());
  ASSERT_TRUE(get_color_buffer_handle_info_result.value().result.is_response());

  const auto& color_buffer_handle_info =
      get_color_buffer_handle_info_result.value().result.response().info;
  EXPECT_NE(color_buffer_handle_info.id(), 0u);
  EXPECT_EQ(color_buffer_handle_info.type(),
            fuchsia_hardware_goldfish::wire::BufferHandleType::kColorBuffer);
  EXPECT_EQ(color_buffer_handle_info.memory_property(),
            fuchsia_hardware_goldfish::wire::kMemoryPropertyDeviceLocal);
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

TEST_F(ControlDeviceTest, GetBufferHandleInfo_Invalid) {
  // Register data buffer, but don't create it.
  {
    constexpr size_t kSize = 65536u;
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    zx_info_handle_basic_t info;
    ASSERT_OK(buffer_vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

    dut_->RegisterBufferHandle(buffer_vmo);

    auto get_buffer_handle_info_result = fidl_client_.GetBufferHandleInfo(std::move(buffer_vmo));
    ASSERT_TRUE(get_buffer_handle_info_result.ok());
    EXPECT_TRUE(get_buffer_handle_info_result.value().result.is_err());
    EXPECT_EQ(get_buffer_handle_info_result.value().result.err(), ZX_ERR_NOT_FOUND);

    dut_->FreeBufferHandle(info.koid);
  }

  // Check non-registered buffer VMO.
  {
    constexpr size_t kSize = 65536u;
    zx::vmo buffer_vmo;
    ASSERT_OK(zx::vmo::create(kSize, 0u, &buffer_vmo));

    auto get_buffer_handle_info_result = fidl_client_.GetBufferHandleInfo(std::move(buffer_vmo));
    ASSERT_TRUE(get_buffer_handle_info_result.ok());
    EXPECT_TRUE(get_buffer_handle_info_result.value().result.is_err());
    EXPECT_EQ(get_buffer_handle_info_result.value().result.err(), ZX_ERR_INVALID_ARGS);
  }

  // Check invalid buffer VMO.
  {
    auto get_buffer_handle_info_result = fidl_client_.GetBufferHandleInfo(zx::vmo());
    ASSERT_EQ(get_buffer_handle_info_result.status(), ZX_ERR_INVALID_ARGS);
  }
}

}  // namespace
}  // namespace goldfish
