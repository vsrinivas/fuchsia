// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/flashmap-client.h"

#include <fidl/fuchsia.acpi.chromeos/cpp/wire.h>
#include <fidl/fuchsia.acpi.chromeos/cpp/wire_test_base.h>
#include <fidl/fuchsia.nand.flashmap/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/rights.h>

#include <zxtest/zxtest.h>

#include "fidl/fuchsia.vboot/cpp/wire.h"
#include "third_party/vboot_reference/firmware/include/gbb_header.h"

namespace {

namespace fmap = fuchsia_nand_flashmap::wire;

constexpr uint32_t kEraseBlockSize = 4096;
// 16KiB of flash for tests.
constexpr uint32_t kFakeFlashSize = 16 * 1024;

struct Area {
  const char* name;
  uint32_t offset;
  uint32_t size;
  bool preserve;
};

constexpr struct Area kDefaultAreas[] = {
    {
        .name = "GBB",
        .offset = 0,
        .size = kEraseBlockSize,
        .preserve = false,
    },
    {
        .name = "RW_SECTION_A",
        .offset = kEraseBlockSize,
        .size = kEraseBlockSize,
        .preserve = false,
    },
    {
        .name = "RW_SECTION_B",
        .offset = 2 * kEraseBlockSize,
        .size = kEraseBlockSize,
        .preserve = false,
    },
};

class FakeCrosAcpi : public fidl::testing::WireTestBase<fuchsia_acpi_chromeos::Device> {
 public:
  using Slot = fuchsia_acpi_chromeos::wire::BootSlot;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    ASSERT_FALSE(true, "Method %s not implemented", name.data());
  }

  void GetActiveApFirmware(GetActiveApFirmwareCompleter::Sync& completer) override {
    completer.ReplySuccess(active_slot_);
  }

  zx::result<fidl::ClientEnd<fuchsia_acpi_chromeos::Device>> GetClient(
      async_dispatcher_t* dispatcher) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_acpi_chromeos::Device>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    fidl::BindServer(dispatcher, std::move(endpoints->server), this);
    return zx::ok(std::move(endpoints->client));
  }

  Slot active_slot_;
};

class FakeFirmwareParam : public fidl::WireServer<fuchsia_vboot::FirmwareParam> {
 public:
  void Get(GetRequestView request, GetCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

  void Set(SetRequestView request, SetCompleter::Sync& completer) override {
    completer.ReplySuccess();
  }

  zx::result<fidl::ClientEnd<fuchsia_vboot::FirmwareParam>> GetClient(
      async_dispatcher_t* dispatcher) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_vboot::FirmwareParam>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    fidl::BindServer(dispatcher, std::move(endpoints->server), this);
    return zx::ok(std::move(endpoints->client));
  }
};

// Fake implementation of a flashmap server, backed by a VMO.
class FakeFlashmap : public fidl::WireServer<fuchsia_nand_flashmap::Flashmap> {
 public:
  explicit FakeFlashmap(uint32_t size, cpp20::span<const Area> areas) : size_(size) {
    ASSERT_OK(zx::vmo::create(size_, 0, &flash_vmo_));
    ASSERT_OK(mapped_vmo_.Map(flash_vmo_, 0, size_));
    memset(mapped_vmo_.start(), 0xff, mapped_vmo_.size());
    for (auto& area : areas) {
      AddArea(area.name, area.size, area.offset, area.preserve);
    }
  }

  zx::result<fidl::ClientEnd<fuchsia_nand_flashmap::Flashmap>> GetClient(
      async_dispatcher_t* dispatcher) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_nand_flashmap::Flashmap>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    fidl::BindServer(dispatcher, std::move(endpoints->server), this);
    return zx::ok(std::move(endpoints->client));
  }

  void AddArea(const char* name, uint32_t size, uint32_t offset, bool preserve);
  void SetAreaContents(const std::string& name, cpp20::span<uint8_t> contents);
  cpp20::span<uint8_t> GetAreaContents(const std::string& name);

  void GetAreas(GetAreasCompleter::Sync& responder) override;
  void GetEraseBlockSize(GetEraseBlockSizeCompleter::Sync& responder) override {
    responder.Reply(kEraseBlockSize);
  }

  void Read(ReadRequestView request, ReadCompleter::Sync& responder) override;
  void Write(WriteRequestView request, WriteCompleter::Sync& responder) override;
  void Erase(EraseRequestView request, EraseCompleter::Sync& responder) override;

  zx::vmo GetFirmwareUpdateVmo() {
    zx::vmo fwupdate;
    ZX_ASSERT(zx::vmo::create(size_, 0, &fwupdate) == ZX_OK);
    ZX_ASSERT(fwupdate.write(mapped_vmo_.start(), 0, mapped_vmo_.size()) == ZX_OK);
    return fwupdate;
  }

  zx::vmo& flash_vmo() { return flash_vmo_; }
  size_t write_calls() const { return write_calls_; }

 private:
  std::vector<fmap::Area> areas_;
  fidl::Arena<> arena_;

  zx::vmo flash_vmo_;
  fzl::VmoMapper mapped_vmo_;
  uint32_t size_;
  size_t write_calls_ = 0;

  std::optional<fmap::Area> FindArea(const std::string& name) {
    for (auto& area : areas_) {
      std::string_view area_name(area.name.begin(), area.name.size());
      if (area_name.compare(name) == 0) {
        return area;
      }
    }
    return std::nullopt;
  }
};

void FakeFlashmap::AddArea(const char* name, uint32_t size, uint32_t offset, bool preserve) {
  fmap::Area area{
      .offset = offset,
      .size = size,
      .name = fidl::StringView(arena_, name),
      .flags = preserve ? fmap::AreaFlags::kPreserve : fmap::AreaFlags::TruncatingUnknown(0),
  };

  ASSERT_LT(offset + size, size_);

  areas_.emplace_back(area);
}

void FakeFlashmap::SetAreaContents(const std::string& name, cpp20::span<uint8_t> contents) {
  auto area = FindArea(name);
  ASSERT_NE(area, std::nullopt);
  ASSERT_LE(contents.size_bytes(), area->size);
  memcpy(static_cast<uint8_t*>(mapped_vmo_.start()) + area->offset, contents.data(),
         contents.size_bytes());
}

cpp20::span<uint8_t> FakeFlashmap::GetAreaContents(const std::string& name) {
  auto area = FindArea(name);
  ZX_ASSERT(area != std::nullopt);
  return cpp20::span<uint8_t>(static_cast<uint8_t*>(mapped_vmo_.start()) + area->offset,
                              area->size);
}

void FakeFlashmap::GetAreas(GetAreasCompleter::Sync& responder) {
  responder.Reply(fidl::VectorView<fmap::Area>::FromExternal(areas_));
}

void FakeFlashmap::Read(ReadRequestView request, ReadCompleter::Sync& responder) {
  auto area = FindArea(std::string(request->name.begin(), request->name.size()));
  if (area == std::nullopt) {
    responder.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  if (request->offset + request->size > area->size) {
    responder.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(request->size, 0, &vmo));

  size_t abs_offset = area->offset + request->offset;
  ASSERT_LT(abs_offset + request->size, mapped_vmo_.size());
  ASSERT_OK(
      vmo.write(reinterpret_cast<uint8_t*>(mapped_vmo_.start()) + abs_offset, 0, request->size));

  responder.ReplySuccess(fuchsia_mem::wire::Range{
      .vmo = std::move(vmo),
      .offset = 0,
      .size = request->size,
  });
}

void FakeFlashmap::Write(WriteRequestView request, WriteCompleter::Sync& responder) {
  write_calls_++;
  auto area = FindArea(std::string(request->name.begin(), request->name.size()));
  if (area == std::nullopt) {
    responder.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  if (request->offset + request->data.size > area->size) {
    responder.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  size_t abs_offset = area->offset + request->offset;
  ASSERT_LT(abs_offset + request->data.size, mapped_vmo_.size());

  std::vector<uint8_t> data(request->data.size, 0xff);
  uint8_t* region_for_write = static_cast<uint8_t*>(mapped_vmo_.start());
  region_for_write += abs_offset;
  // Make sure the region to be written has been erased.
  // This is a very simplistic check, but it works for now.
  ASSERT_BYTES_EQ(data.data(), region_for_write, request->data.size);

  ASSERT_OK(request->data.vmo.read(reinterpret_cast<uint8_t*>(mapped_vmo_.start()) + abs_offset, 0,
                                   request->data.size));
  responder.ReplySuccess();
}

void FakeFlashmap::Erase(EraseRequestView request, EraseCompleter::Sync& responder) {
  auto area = FindArea(std::string(request->name.begin(), request->name.size()));
  if (area == std::nullopt) {
    responder.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  if (request->offset + request->range > area->size) {
    responder.ReplyError(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  size_t abs_offset = area->offset + request->offset;
  ASSERT_LT(abs_offset + request->range, mapped_vmo_.size());
  if (request->range % kEraseBlockSize != 0 || abs_offset % kEraseBlockSize != 0) {
    responder.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  memset(static_cast<uint8_t*>(mapped_vmo_.start()) + abs_offset, 0xff, request->range);
  responder.ReplySuccess();
}

std::vector<uint8_t> MakeGbb(const char* hwid, uint8_t key_byte) {
  std::vector<uint8_t> buf(kEraseBlockSize, 0xff);
  constexpr uint32_t kKeySize = 32;
  // Make sure everything fits in the buffer.
  ZX_ASSERT(strlen(hwid) + 1 + sizeof(GoogleBinaryBlockHeader) + (2ul * kKeySize) <=
            kEraseBlockSize);

  uint8_t* ptr = buf.data();

  GoogleBinaryBlockHeader* gbb = reinterpret_cast<GoogleBinaryBlockHeader*>(ptr);
  gbb->header_size = GBB_HEADER_SIZE;
  gbb->major_version = GBB_MAJOR_VER;
  gbb->minor_version = GBB_MINOR_VER;
  memcpy(gbb->signature, GBB_SIGNATURE, GBB_SIGNATURE_SIZE);

  // We don't care about bmpfv_* for our tests.
  gbb->bmpfv_offset = GBB_HEADER_SIZE;
  gbb->bmpfv_size = 0;
  // Keys.
  gbb->rootkey_offset = GBB_HEADER_SIZE;
  gbb->rootkey_size = kKeySize;
  gbb->recovery_key_offset = gbb->rootkey_offset + gbb->rootkey_size;
  gbb->recovery_key_size = kKeySize;
  // HWID goes at the end.
  gbb->hwid_offset = gbb->recovery_key_offset + gbb->recovery_key_size;
  gbb->hwid_size = static_cast<uint32_t>(strlen(hwid)) + 1;

  uint8_t* work_ptr = ptr + gbb->rootkey_offset;
  memset(work_ptr, key_byte, gbb->rootkey_size);
  work_ptr = ptr + gbb->recovery_key_offset;
  memset(work_ptr, key_byte, gbb->recovery_key_size);

  work_ptr = ptr + gbb->hwid_offset;
  memcpy(work_ptr, hwid, gbb->hwid_size);

  return buf;
}

class FlashmapClientTest : public zxtest::Test {
 public:
  FlashmapClientTest()
      : flashmap_(kFakeFlashSize, kDefaultAreas), loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    ASSERT_OK(loop_.StartThread("test-fidl-loop"));
    auto gbb = MakeGbb("FUCHSIA TEST 1412", 0xab);
    flashmap_.SetAreaContents("GBB", gbb);
    auto fmap = flashmap_.GetClient(loop_.dispatcher());
    ASSERT_OK(fmap.status_value());
    auto cros_acpi = cros_acpi_.GetClient(loop_.dispatcher());
    ASSERT_OK(cros_acpi.status_value());
    auto fwparam = fwparam_.GetClient(loop_.dispatcher());
    ASSERT_OK(fwparam.status_value());

    auto client = paver::FlashmapPartitionClient::CreateWithClients(
        std::move(fmap.value()), std::move(cros_acpi.value()), std::move(fwparam.value()));
    ASSERT_OK(client.status_value());

    client_ = std::move(client.value());
  }

 protected:
  FakeFirmwareParam fwparam_;
  FakeCrosAcpi cros_acpi_;
  FakeFlashmap flashmap_;
  async::Loop loop_;
  std::unique_ptr<paver::FlashmapPartitionClient> client_;
};

TEST_F(FlashmapClientTest, TestNoUpdateNeeded) {
  cros_acpi_.active_slot_ = FakeCrosAcpi::Slot::kA;

  // Set up the firmware image.
  // First, we create the image in the update package - to be passed to
  // FlashmapPartitionClient::Write(). Then we modify it so that it's the "installed" SPI flash.
  std::vector<uint8_t> firmware_image(256, 0xaa);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);

  // Make a copy of the flashmap so we install it.
  zx::vmo new_image;
  ASSERT_OK(flashmap_.flash_vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &new_image));

  // Change section B in the "installed" firmware image to be different. This is to make sure that
  // the partition client is comparing the correct instance of the firmware.
  memset(firmware_image.data(), 0xbc, firmware_image.size());
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);

  auto status = client_->Write(new_image, kFakeFlashSize);
  ASSERT_OK(status.status_value());
  // Make sure that nothing changed.
  ASSERT_EQ(flashmap_.write_calls(), 0);
}

TEST_F(FlashmapClientTest, TestFirmwareUpdate) {
  cros_acpi_.active_slot_ = FakeCrosAcpi::Slot::kA;

  std::vector<uint8_t> firmware_image(256, 0xaa);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);

  zx::vmo new_image = flashmap_.GetFirmwareUpdateVmo();

  memset(firmware_image.data(), 0xbc, firmware_image.size());
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);

  auto status = client_->Write(new_image, kFakeFlashSize);
  ASSERT_OK(status.status_value());
  ASSERT_EQ(flashmap_.write_calls(), 1);
  // Make sure that the "A" slot (which is active) was left as-is, and the "B" slot contains the new
  // image.
  ASSERT_BYTES_EQ(flashmap_.GetAreaContents("RW_SECTION_A").data(), firmware_image.data(),
                  firmware_image.size());
  memset(firmware_image.data(), 0xaa, firmware_image.size());
  ASSERT_BYTES_EQ(flashmap_.GetAreaContents("RW_SECTION_B").data(), firmware_image.data(),
                  firmware_image.size());
}

TEST_F(FlashmapClientTest, TestFirmwareUpdateFromRecovery) {
  cros_acpi_.active_slot_ = FakeCrosAcpi::Slot::kRecovery;

  std::vector<uint8_t> firmware_image(256, 0xaa);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);

  zx::vmo new_image = flashmap_.GetFirmwareUpdateVmo();

  memset(firmware_image.data(), 0xbc, firmware_image.size());
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);

  auto status = client_->Write(new_image, kFakeFlashSize);
  ASSERT_STATUS(status.status_value(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(FlashmapClientTest, TestFirmwareUpdateWrongBoardID) {
  cros_acpi_.active_slot_ = FakeCrosAcpi::Slot::kA;

  std::vector<uint8_t> firmware_image(256, 0xaa);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);

  zx::vmo new_image = flashmap_.GetFirmwareUpdateVmo();

  memset(firmware_image.data(), 0xbc, firmware_image.size());
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  auto gbb = MakeGbb("EVE TEST 1412", 0xab);
  flashmap_.SetAreaContents("GBB", gbb);

  // Firmware update should succeed but not touch the firmware.
  auto status = client_->Write(new_image, kFakeFlashSize);
  ASSERT_OK(status.status_value());
  ASSERT_EQ(flashmap_.write_calls(), 0);
}

TEST_F(FlashmapClientTest, TestFirmwareUpdateWrongKey) {
  cros_acpi_.active_slot_ = FakeCrosAcpi::Slot::kA;

  std::vector<uint8_t> firmware_image(256, 0xaa);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);

  zx::vmo new_image = flashmap_.GetFirmwareUpdateVmo();

  memset(firmware_image.data(), 0xbc, firmware_image.size());
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  auto gbb = MakeGbb("FUCHSIA TEST 1412", 0xbb);
  flashmap_.SetAreaContents("GBB", gbb);

  auto status = client_->Write(new_image, kFakeFlashSize);
  ASSERT_OK(status.status_value());
  ASSERT_EQ(flashmap_.write_calls(), 0);
}

TEST_F(FlashmapClientTest, TestFirmwareUpdateHWIDCompatible) {
  cros_acpi_.active_slot_ = FakeCrosAcpi::Slot::kA;

  std::vector<uint8_t> firmware_image(256, 0xaa);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);

  zx::vmo new_image = flashmap_.GetFirmwareUpdateVmo();

  memset(firmware_image.data(), 0xbc, firmware_image.size());
  flashmap_.SetAreaContents("RW_SECTION_B", firmware_image);
  flashmap_.SetAreaContents("RW_SECTION_A", firmware_image);
  auto gbb = MakeGbb("FUCHSIA A8K-BDP", 0xab);
  flashmap_.SetAreaContents("GBB", gbb);

  auto status = client_->Write(new_image, kFakeFlashSize);
  ASSERT_OK(status.status_value());
  ASSERT_EQ(flashmap_.write_calls(), 1);
  // Make sure that the "A" slot (which is active) was left as-is, and the "B" slot contains the new
  // image.
  ASSERT_BYTES_EQ(flashmap_.GetAreaContents("RW_SECTION_A").data(), firmware_image.data(),
                  firmware_image.size());
  memset(firmware_image.data(), 0xaa, firmware_image.size());
  ASSERT_BYTES_EQ(flashmap_.GetAreaContents("RW_SECTION_B").data(), firmware_image.data(),
                  firmware_image.size());
}

}  // namespace
