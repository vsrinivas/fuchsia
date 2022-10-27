// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.nand/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <cstddef>
#include <new>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

#include "parent.h"

namespace {

constexpr uint32_t kMinOobSize = 4;
constexpr uint32_t kMinBlockSize = 4;
constexpr uint32_t kMinNumBlocks = 5;
constexpr uint32_t kInMemoryPages = 20;

// The device under test.
class NandDevice {
 public:
  static zx::result<NandDevice> Create() {
    ParentDevice& parent = *g_parent_device_;

    MaybeOwned<fuchsia_device::Controller> controller;
    if (parent.IsBroker()) {
      controller = parent.controller().borrow();
    } else {
      constexpr char kBroker[] = "nand-broker.so";
      const fidl::WireResult result =
          fidl::WireCall(parent.controller().borrow())->Rebind(fidl::StringView(kBroker));
      if (!result.ok()) {
        return zx::error(result.status());
      }
      const fit::result response = result.value();
      if (response.is_error()) {
        return zx::error(response.error_value());
      }

      // Get the new child.
      fbl::unique_fd dir(open(parent.Path(), O_RDONLY | O_DIRECTORY));
      fbl::unique_fd fd;
      if (zx_status_t status = device_watcher::RecursiveWaitForFile(dir, "broker", &fd);
          status != ZX_OK) {
        return zx::error(status);
      }
      fdio_cpp::FdioCaller caller(std::move(fd));
      zx::result child = caller.take_as<fuchsia_device::Controller>();
      if (child.is_error()) {
        return child.take_error();
      }
      controller = std::move(child.value());
    }

    if (parent.IsExternal()) {
      // This looks like using code under test to setup the test, but this
      // path is for external devices, not really the broker. The issue is that
      // ParentDevice cannot query a nand device for the actual parameters.
      const fidl::WireResult result =
          fidl::WireCall(fidl::UnownedClientEnd<fuchsia_nand::Broker>(Get(controller).channel()))
              ->GetInfo();
      if (!result.ok()) {
        printf("failed to query nand device: %s\n", result.FormatDescription().c_str());
        return zx::error(result.status());
      }
      const fidl::WireResponse response = result.value();
      if (zx_status_t status = response.status; status != ZX_OK) {
        printf("failed to query nand device: %s\n", zx_status_get_string(status));
        return zx::error(status);
      }
      parent.SetInfo(*response.info);
    }

    if (parent.Info().oob_size < kMinOobSize) {
      fprintf(stderr, "%s:%d parent=%d min=%d\n", __FILE_NAME__, __LINE__, parent.Info().oob_size,
              kMinOobSize);
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (parent.Info().pages_per_block < kMinBlockSize) {
      fprintf(stderr, "%s:%d\n", __FILE_NAME__, __LINE__);
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (parent.NumBlocks() < kMinNumBlocks) {
      fprintf(stderr, "%s:%d parent=%d min=%d\n", __FILE_NAME__, __LINE__, parent.NumBlocks(),
              kMinNumBlocks);
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (parent.NumBlocks() + parent.FirstBlock() > parent.Info().num_blocks) {
      fprintf(stderr, "%s:%d\n", __FILE_NAME__, __LINE__);
      return zx::error(ZX_ERR_INVALID_ARGS);
    }

    uint32_t num_blocks = parent.NumBlocks();
    ;
    bool full_device = num_blocks == parent.Info().num_blocks;
    if (!full_device) {
      // Not using the whole device, don't need to test all limits.
      num_blocks = std::min(num_blocks, kMinNumBlocks);
    }
    return zx::ok(NandDevice(parent, std::move(controller), num_blocks, full_device));
  }

  fidl::UnownedClientEnd<fuchsia_device::Controller> controller() { return Get(controller_); }

  // Provides a channel to issue fidl calls.
  fidl::UnownedClientEnd<fuchsia_nand::Broker> channel() {
    return fidl::UnownedClientEnd<fuchsia_nand::Broker>(controller().channel());
  }

  // Wrappers for "queue" operations that take care of preserving the vmo's handle
  // and translating the request to the desired block range on the actual device.
  zx_status_t Read(const zx::vmo& vmo, fuchsia_nand::wire::BrokerRequestData request);
  zx_status_t Write(const zx::vmo& vmo, fuchsia_nand::wire::BrokerRequestData request);
  zx_status_t ReadBytes(const zx::vmo& vmo, fuchsia_nand::wire::BrokerRequestDataBytes request);
  zx_status_t WriteBytes(const zx::vmo& vmo, fuchsia_nand::wire::BrokerRequestDataBytes request);
  zx_status_t Erase(fuchsia_nand::wire::BrokerRequestData request);

  // Erases a given block number.
  zx_status_t EraseBlock(uint32_t block_num);

  // Verifies that the buffer pointed to by the operation's vmo contains the given
  // pattern for the desired number of pages, skipping the pages before start.
  bool CheckPattern(uint8_t expected, int start, int num_pages, const void* memory) const;

  const fuchsia_hardware_nand::wire::Info& Info() const { return parent_.Info(); }

  uint32_t PageSize() const { return parent_.Info().page_size; }
  uint32_t OobSize() const { return parent_.Info().oob_size; }
  uint32_t BlockSize() const { return parent_.Info().pages_per_block; }
  uint32_t NumBlocks() const { return num_blocks_; }
  uint32_t NumPages() const { return NumBlocks() * BlockSize(); }
  uint32_t MaxBufferSize() const { return kInMemoryPages * (PageSize() + OobSize()); }

  // True when the whole device under test can be modified.
  bool IsFullDevice() const { return full_device_; }

 private:
  template <typename Protocol>
  using MaybeOwned = std::variant<fidl::ClientEnd<Protocol>, fidl::UnownedClientEnd<Protocol>>;

  NandDevice(ParentDevice& parent, MaybeOwned<fuchsia_device::Controller> controller,
             uint32_t num_blocks, bool full_device)
      : parent_(parent),
        controller_(std::move(controller)),
        num_blocks_(num_blocks),
        full_device_(full_device) {}

  template <typename Protocol>
  static fidl::UnownedClientEnd<Protocol> Get(const MaybeOwned<Protocol>& variant) {
    return std::visit(
        [](auto&& arg) -> fidl::UnownedClientEnd<Protocol> {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, fidl::ClientEnd<Protocol>>) {
            return arg;
          } else if constexpr (std::is_same_v<T, fidl::UnownedClientEnd<Protocol>>) {
            return arg;
          }
        },
        variant);
  }

  ParentDevice& parent_;
  MaybeOwned<fuchsia_device::Controller> controller_;
  const uint32_t num_blocks_;
  const bool full_device_;
};

zx_status_t NandDevice::Read(const zx::vmo& vmo, fuchsia_nand::wire::BrokerRequestData request) {
  if (!full_device_) {
    request.offset_nand = request.offset_nand + parent_.FirstBlock() * BlockSize();
    ZX_DEBUG_ASSERT(request.offset_nand < NumPages());
    ZX_DEBUG_ASSERT(request.offset_nand + request.length <= NumPages());
  }

  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &request.vmo);
  if (status != ZX_OK) {
    return status;
  }
  const fidl::WireResult result = fidl::WireCall(channel())->Read(std::move(request));
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.status;
}

zx_status_t NandDevice::ReadBytes(const zx::vmo& vmo,
                                  fuchsia_nand::wire::BrokerRequestDataBytes request) {
  if (!full_device_) {
    request.offset_nand = request.offset_nand +
                          static_cast<uint64_t>(parent_.FirstBlock()) * BlockSize() * PageSize();
    ZX_DEBUG_ASSERT(request.offset_nand < NumPages());
    ZX_DEBUG_ASSERT(request.offset_nand + request.length <= NumPages());
  }

  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &request.vmo);
  if (status != ZX_OK) {
    return status;
  }
  const fidl::WireResult result = fidl::WireCall(channel())->ReadBytes(std::move(request));
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.status;
}

zx_status_t NandDevice::Write(const zx::vmo& vmo, fuchsia_nand::wire::BrokerRequestData request) {
  if (!full_device_) {
    request.offset_nand = request.offset_nand + parent_.FirstBlock() * BlockSize();
    ZX_DEBUG_ASSERT(request.offset_nand < static_cast<uint64_t>(NumPages()) * PageSize());
    ZX_DEBUG_ASSERT(request.offset_nand + request.length <=
                    static_cast<uint64_t>(NumPages()) * PageSize());
  }

  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &request.vmo);
  if (status != ZX_OK) {
    return status;
  }
  const fidl::WireResult result = fidl::WireCall(channel())->Write(std::move(request));
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.status;
}

zx_status_t NandDevice::WriteBytes(const zx::vmo& vmo,
                                   fuchsia_nand::wire::BrokerRequestDataBytes request) {
  if (!full_device_) {
    request.offset_nand = request.offset_nand +
                          static_cast<uint64_t>(parent_.FirstBlock()) * BlockSize() * PageSize();
    ZX_DEBUG_ASSERT(request.offset_nand < static_cast<uint64_t>(NumPages()) * PageSize());
    ZX_DEBUG_ASSERT(request.offset_nand + request.length <=
                    static_cast<uint64_t>(NumPages()) * PageSize());
  }

  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &request.vmo);
  if (status != ZX_OK) {
    return status;
  }
  const fidl::WireResult result = fidl::WireCall(channel())->WriteBytes(std::move(request));
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.status;
}

zx_status_t NandDevice::Erase(fuchsia_nand::wire::BrokerRequestData request) {
  if (!full_device_) {
    request.offset_nand = request.offset_nand + parent_.FirstBlock();
    ZX_DEBUG_ASSERT(request.offset_nand < NumBlocks());
    ZX_DEBUG_ASSERT(request.offset_nand + request.length <= NumBlocks());
  }

  const fidl::WireResult result = fidl::WireCall(channel())->Erase(std::move(request));
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.status;
}

zx_status_t NandDevice::EraseBlock(uint32_t block_num) {
  return Erase({
      .length = 1,
      .offset_nand = block_num,
  });
}

bool NandDevice::CheckPattern(uint8_t expected, int start, int num_pages,
                              const void* memory) const {
  const uint8_t* buffer = reinterpret_cast<const uint8_t*>(memory) + PageSize() * start;
  for (uint32_t i = 0; i < PageSize() * num_pages; i++) {
    if (buffer[i] != expected) {
      return false;
    }
  }
  return true;
}

TEST(NandBrokerTest, TrivialLifetime) {
  zx::result<NandDevice> result = NandDevice::Create();
  ASSERT_OK(result.status_value());
}

TEST(NandBrokerTest, Query) {
  zx::result result = NandDevice::Create();
  ASSERT_OK(result.status_value());
  NandDevice& device = result.value();

  {
    const fidl::WireResult result = fidl::WireCall(device.channel())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_nand::wire::Info& info = *response.info;

    EXPECT_EQ(device.Info().page_size, info.page_size);
    EXPECT_EQ(device.Info().oob_size, info.oob_size);
    EXPECT_EQ(device.Info().pages_per_block, info.pages_per_block);
    EXPECT_EQ(device.Info().num_blocks, info.num_blocks);
    EXPECT_EQ(device.Info().ecc_bits, info.ecc_bits);
    EXPECT_EQ(device.Info().nand_class, info.nand_class);
  }
}

TEST(NandBrokerTest, ReadWriteLimits) {
  zx::result result = NandDevice::Create();
  ASSERT_OK(result.status_value());
  NandDevice& device = result.value();

  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap(device.MaxBufferSize(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                &vmo));

  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Read(vmo, {}));
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Write(vmo, {}));

  if (device.IsFullDevice()) {
    {
      auto request = [&device]() -> fuchsia_nand::wire::BrokerRequestData {
        return {
            .length = 1,
            .offset_nand = device.NumPages(),
        };
      };

      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Read(vmo, request()));
      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Write(vmo, request()));
    }

    {
      auto request = [&device]() -> fuchsia_nand::wire::BrokerRequestData {
        return {
            .length = 2,
            .offset_nand = device.NumPages() - 1,
        };
      };

      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Read(vmo, request()));
      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Write(vmo, request()));
    }
  }

  auto request = [&device]() -> fuchsia_nand::wire::BrokerRequestData {
    return {
        .length = 1,
        .offset_nand = device.NumPages() - 1,
    };
  };

  EXPECT_EQ(ZX_ERR_BAD_HANDLE, device.Read(vmo, request()));
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, device.Write(vmo, request()));

  auto request_with_data_vmo = [request]() {
    fuchsia_nand::wire::BrokerRequestData base = request();
    base.data_vmo = true;
    return base;
  };

  EXPECT_OK(device.Read(vmo, request_with_data_vmo()));
  EXPECT_OK(device.Write(vmo, request_with_data_vmo()));
}

TEST(NandBrokerTest, EraseLimits) {
  zx::result result = NandDevice::Create();
  ASSERT_OK(result.status_value());
  NandDevice& device = result.value();

  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Erase({}));

  if (device.IsFullDevice()) {
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Erase({
                                       .length = 1,
                                       .offset_nand = device.NumBlocks(),
                                   }));

    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device.Erase({
                                       .length = 2,
                                       .offset_nand = device.NumBlocks() - 1,
                                   }));
  }

  EXPECT_OK(device.Erase({
      .length = 1,
      .offset_nand = device.NumBlocks() - 1,
  }));
}

TEST(NandBrokerTest, ReadWrite) {
  zx::result result = NandDevice::Create();
  ASSERT_OK(result.status_value());
  NandDevice& device = result.value();

  ASSERT_OK(device.EraseBlock(0));

  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap(device.MaxBufferSize(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                &vmo));
  memset(mapper.start(), 0x55, mapper.size());

  auto request = []() -> fuchsia_nand::wire::BrokerRequestData {
    return {
        .length = 4,
        .offset_nand = 4,
        .data_vmo = true,
    };
  };

  ASSERT_OK(device.Write(vmo, request()));

  memset(mapper.start(), 0, mapper.size());

  ASSERT_OK(device.Read(vmo, request()));
  ASSERT_TRUE(device.CheckPattern(0x55, 0, 4, mapper.start()));
}

TEST(NandBrokerTest, ReadWriteOob) {
  zx::result result = NandDevice::Create();
  ASSERT_OK(result.status_value());
  NandDevice& device = result.value();

  ASSERT_OK(device.EraseBlock(0));

  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap(device.MaxBufferSize(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                &vmo));
  const char desired[] = {'a', 'b', 'c', 'd'};
  memcpy(mapper.start(), desired, sizeof(desired));

  auto request = []() -> fuchsia_nand::wire::BrokerRequestData {
    return {
        .length = 1,
        .offset_nand = 2,
        .oob_vmo = true,
    };
  };

  ASSERT_OK(device.Write(vmo, request()));

  memset(mapper.start(), 0, device.OobSize() * 2);

  ASSERT_OK(device.Read(vmo, [request]() {
    fuchsia_nand::wire::BrokerRequestData base = request();
    base.length = 2;
    base.offset_nand = 1;
    return base;
  }()));

  // The "second page" has the data of interest.
  ASSERT_EQ(0, memcmp(reinterpret_cast<char*>(mapper.start()) + device.OobSize(), desired,
                      sizeof(desired)));
}

TEST(NandBrokerTest, ReadWriteDataAndOob) {
  zx::result result = NandDevice::Create();
  ASSERT_OK(result.status_value());
  NandDevice& device = result.value();

  ASSERT_OK(device.EraseBlock(0));

  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap(device.MaxBufferSize(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                &vmo));

  char* buffer = reinterpret_cast<char*>(mapper.start());
  memset(buffer, 0x55, device.PageSize() * 2);
  memset(buffer + device.PageSize() * 2, 0xaa, device.OobSize() * 2);

  auto request = []() -> fuchsia_nand::wire::BrokerRequestData {
    return {
        .length = 2,
        .offset_nand = 2,
        .offset_oob_vmo = 2,  // OOB is right after data.
        .data_vmo = true,
        .oob_vmo = true,
    };
  };

  ASSERT_OK(device.Write(vmo, request()));

  memset(buffer, 0, device.PageSize() * 4);
  ASSERT_OK(device.Read(vmo, request()));

  // Verify data.
  ASSERT_TRUE(device.CheckPattern(0x55, 0, 2, buffer));

  // Verify OOB.
  memset(buffer, 0xaa, device.PageSize());
  ASSERT_EQ(0, memcmp(buffer + device.PageSize() * 2, buffer, device.OobSize() * 2));
}

TEST(NandBrokerTest, Erase) {
  zx::result result = NandDevice::Create();
  ASSERT_OK(result.status_value());
  NandDevice& device = result.value();

  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap(device.MaxBufferSize(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                &vmo));

  memset(mapper.start(), 0x55, mapper.size());

  auto request = [&device]() -> fuchsia_nand::wire::BrokerRequestData {
    return {
        .length = kMinBlockSize,
        .offset_nand = device.BlockSize(),
        .data_vmo = true,
    };
  };
  ASSERT_OK(device.Write(vmo, request()));

  auto request_with_double_offset = [request]() {
    fuchsia_nand::wire::BrokerRequestData base = request();
    base.offset_nand *= 2;
    return base;
  };
  ASSERT_OK(device.Write(vmo, request_with_double_offset()));

  ASSERT_OK(device.EraseBlock(1));
  ASSERT_OK(device.EraseBlock(2));

  ASSERT_OK(device.Read(vmo, request_with_double_offset()));
  ASSERT_TRUE(device.CheckPattern(0xff, 0, kMinBlockSize, mapper.start()));

  ASSERT_OK(device.Read(vmo, request()));
  ASSERT_TRUE(device.CheckPattern(0xff, 0, kMinBlockSize, mapper.start()));
}

TEST(NandBrokerTest, ReadWriteDataBytes) {
  zx::result result = NandDevice::Create();
  ASSERT_OK(result.status_value());
  NandDevice& device = result.value();

  ASSERT_OK(device.EraseBlock(0));

  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap(device.MaxBufferSize(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                &vmo));

  char* buffer = reinterpret_cast<char*>(mapper.start());
  memset(buffer, 0x55, 2);

  auto request = []() -> fuchsia_nand::wire::BrokerRequestDataBytes {
    return {
        .length = 2,
        .offset_nand = 2,
    };
  };

  ASSERT_OK(device.WriteBytes(vmo, request()));

  memset(buffer, 0, 4);
  ASSERT_OK(device.ReadBytes(vmo, request()));

  constexpr uint8_t kExpected[] = {0x55, 0x55};
  // Verify data.
  ASSERT_BYTES_EQ(buffer, kExpected, 2);
}

}  // namespace
