// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/flashmap-client.h"

#include <fidl/fuchsia.acpi.chromeos/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.vboot/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/component/cpp/service_client.h>
#include <zircon/errors.h>

#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"
#include "third_party/vboot_reference/firmware/include/gbb_header.h"

namespace paver {
constexpr const char kGbbAreaName[] = "GBB";
constexpr const char kFirmwareRwASection[] = "RW_SECTION_A";
constexpr const char kFirmwareRwBSection[] = "RW_SECTION_B";
constexpr const char kChromeOsAcpiClassPath[] = "class/chromeos-acpi/";
constexpr const char kNandClassPath[] = "class/nand/";

static const char* GetHWID(const GoogleBinaryBlockHeader* gbb) {
  const char* gbb_p = reinterpret_cast<const char*>(gbb);
  const char* hwid = gbb_p + gbb->hwid_offset;
  return hwid;
}

static zx::status<fidl::ClientEnd<fuchsia_nand::Broker>> ConnectToBroker(
    const fbl::unique_fd& devfs_root, zx::channel& device) {
  // Get the topological path of the NAND device so we can figure out where the broker is.
  auto path = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(device.borrow()))
                  ->GetTopologicalPath();
  if (!path.ok() || path->is_error()) {
    return zx::error(path.ok() ? path->error_value() : path.status());
  }

  // Strip the leading "/dev/" from GetTopologicalPath's response.
  auto broker_path = std::string(path->value()->path.data(), path->value()->path.size())
                         .substr(5)
                         .append("/broker");

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Connect to the broker.
  fdio_cpp::UnownedFdioCaller caller(devfs_root.get());
  status = fdio_service_connect_at(caller.borrow_channel(), broker_path.data(), remote.release());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(local));
}

// Validate the given GBB is valid and is smaller than |buffer_size|.
static zx_status_t ValidateGbb(GoogleBinaryBlockHeader* hdr, size_t buffer_size) {
  if (memcmp(hdr->signature, GBB_SIGNATURE, GBB_SIGNATURE_SIZE) != 0) {
    ERROR("Invalid GBB signature.\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (hdr->major_version != GBB_MAJOR_VER) {
    ERROR("Invalid GBB major version.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (hdr->minor_version < GBB_MINOR_VER) {
    ERROR("Invalid GBB minor version.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (hdr->header_size != GBB_HEADER_SIZE || hdr->header_size > buffer_size) {
    ERROR("GBB header has wrong size.\n");
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // Make sure that nothing goes beyond the end of the buffer.
  size_t max_offset = std::max(
      {hdr->header_size, hdr->hwid_offset + hdr->hwid_size, hdr->rootkey_offset + hdr->rootkey_size,
       hdr->bmpfv_offset + hdr->bmpfv_size, hdr->recovery_key_offset + hdr->recovery_key_size});
  if (max_offset > buffer_size) {
    ERROR("GBB goes beyond end of buffer.\n");
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // Make sure that everything that should go after the header is actually after the header.
  if (hdr->hwid_offset < hdr->header_size || hdr->bmpfv_offset < hdr->header_size ||
      hdr->recovery_key_offset < hdr->header_size || hdr->rootkey_offset < hdr->header_size) {
    ERROR("GBB data overlaps with header.\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // Make sure that the HWID is null terminated.
  const char* hwid = GetHWID(hdr);
  bool ok = false;
  for (size_t i = 0; i < hdr->hwid_size; i++) {
    if (hwid[i] == 0) {
      ok = true;
      break;
    }
  }
  if (!ok) {
    ERROR("GBB HWID is not null terminated.\n");
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx::status<std::unique_ptr<FlashmapPartitionClient>> FlashmapPartitionClient::Create(
    const fbl::unique_fd& devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
    zx::duration timeout) {
  // Connect to the flashmap manager service.
  auto client_end = component::ConnectAt<fuchsia_nand_flashmap::Manager>(svc_root);
  if (client_end.is_error()) {
    ERROR("Failed to connect to flashmap manager: %s\n", client_end.status_string());
    return client_end.take_error();
  }
  fidl::WireSyncClient<fuchsia_nand_flashmap::Manager> manager(std::move(*client_end));

  // Find the NAND device. For now, we just assume that it's the first NAND device.
  auto result = OpenPartition(
      devfs_root, kNandClassPath, [](const zx::channel& chan) { return false; }, timeout.get());
  if (result.is_error()) {
    ERROR("Could not find NAND device: %s\n", result.status_string());
    return result.take_error();
  }

  // Connect to the NAND broker that is bound to this device.
  auto broker = ConnectToBroker(devfs_root, result.value());
  if (broker.is_error()) {
    ERROR("Could not bind broker\n");
    return broker.take_error();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_nand_flashmap::Flashmap>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  // Start the flashmap service on this NAND device.
  auto result2 = manager->Start(std::move(*broker), std::move(endpoints->server));
  if (!result2.ok()) {
    ERROR("Could not start manager\n");
    return zx::error(result2.status());
  }

  // Connect to the ACPI device.
  auto cros_result = OpenPartition(
      devfs_root, kChromeOsAcpiClassPath, [](const zx::channel&) { return false; }, timeout.get());
  if (cros_result.is_error()) {
    ERROR("Could not find chromeos-acpi device: %s\n", cros_result.status_string());
    return cros_result.take_error();
  }

  fidl::ClientEnd<fuchsia_acpi_chromeos::Device> cros_acpi(std::move(cros_result.value()));

  // Connect to the firmware parameter service.
  auto fwparam_client = component::Connect<fuchsia_vboot::FirmwareParam>();
  if (fwparam_client.is_error()) {
    return fwparam_client.take_error();
  }

  return CreateWithClients(std::move(endpoints->client), std::move(cros_acpi),
                           std::move(fwparam_client.value()));
}

zx::status<std::unique_ptr<FlashmapPartitionClient>> FlashmapPartitionClient::CreateWithClients(
    fidl::ClientEnd<fuchsia_nand_flashmap::Flashmap> flashmap,
    fidl::ClientEnd<fuchsia_acpi_chromeos::Device> cros_acpi,
    fidl::ClientEnd<fuchsia_vboot::FirmwareParam> fwparam) {
  auto client = std::make_unique<FlashmapPartitionClient>(std::move(flashmap), std::move(cros_acpi),
                                                          std::move(fwparam));
  auto areas_ok = client->Init();
  if (areas_ok.is_error()) {
    return areas_ok.take_error();
  }
  return zx::ok(std::move(client));
}

zx::status<> FlashmapPartitionClient::Init() {
  ZX_DEBUG_ASSERT(areas_.empty());
  auto areas = flashmap_->GetAreas();
  if (!areas.ok()) {
    return zx::error(areas.status());
  }

  for (auto& area : areas.value().areas) {
    areas_.emplace_back(area);
  }

  auto erase_block_size = flashmap_->GetEraseBlockSize();
  if (!erase_block_size.ok()) {
    return zx::error(erase_block_size.status());
  }
  erase_block_size_ = erase_block_size.value().erase_block_size;

  return zx::ok();
}

zx::status<size_t> FlashmapPartitionClient::GetPartitionSize() {
  // The first area covers the entire flash.
  return zx::ok(areas_[0].size);
}

zx::status<> FlashmapPartitionClient::Read(const zx::vmo& vmo, size_t size) {
  // We can't read the entire flash - things like the ME firmware are inaccessible (so any kind of
  // comparison by the paver would be meaningless).
  // For now, we don't implement this.
  // Write() contains all the logic necessary to update sections of the flash.
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> FlashmapPartitionClient::Write(const zx::vmo& vmo, size_t vmo_size) {
  if (vmo_size < areas_[0].size) {
    ERROR("FlashmapPartitionClient expects a full firmware image. (got 0x%zx)\n", vmo_size);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  // Map in the VMO to make other logic simpler.
  fzl::VmoMapper new_image;
  zx_status_t status = new_image.Map(vmo, /*offset=*/0, /*size=*/vmo_size);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Read the current GBB.
  auto area = FindArea(kGbbAreaName);
  if (!area.has_value()) {
    ERROR("Could not find the GBB.\n");
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  auto current_gbb = flashmap_->Read(kGbbAreaName, 0, area->size);
  if (!current_gbb.ok()) {
    return zx::error(current_gbb.status());
  }
  if (current_gbb->is_error()) {
    return zx::error(current_gbb->error_value());
  }

  // Map in the current GBB.
  fzl::VmoMapper gbb_mapper;
  status = gbb_mapper.Map(current_gbb->value()->range.vmo);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  // Make sure that the current GBB is actually valid.
  GoogleBinaryBlockHeader* cur_gbb = reinterpret_cast<GoogleBinaryBlockHeader*>(
      reinterpret_cast<uintptr_t>(gbb_mapper.start()) + current_gbb->value()->range.offset);
  status = ValidateGbb(cur_gbb, current_gbb->value()->range.size);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Make sure that the new GBB is actually valid.
  GoogleBinaryBlockHeader* new_gbb = reinterpret_cast<GoogleBinaryBlockHeader*>(
      reinterpret_cast<uintptr_t>(new_image.start()) + area->offset);
  status = ValidateGbb(new_gbb, area->size);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Check the HWIDs match.
  if (!IsHWIDCompatible(cur_gbb, new_gbb)) {
    LOG("New firmware image is not for this device. This is a %s, new firmware image is for %s. "
        "Skipping firmware upgrade.\n",
        GetHWID(cur_gbb), GetHWID(new_gbb));
    // Refuse to install the incompatible firmware image.
    return zx::ok();
  }

  // Determine if we need to perform a "full" update (including the RO section), or just update a
  // single slot.
  bool needs_full_update = NeedsFullUpdate(cur_gbb, new_gbb);
  if (needs_full_update) {
    ERROR("Full update is not yet implemented. https://fxbug.dev/81685\n");
    return FullUpdate(new_image);
  }

  return ABUpdate(new_image);
}

zx::status<> FlashmapPartitionClient::ABUpdate(fzl::VmoMapper& new_image) {
  // First: determine which slot we booted from.
  auto active_slot = cros_acpi_->GetActiveApFirmware();
  if (!active_slot.ok() || active_slot->is_error()) {
    ERROR("Failed to send active firmware slot request: %s\n",
          active_slot.ok() ? zx_status_get_string(active_slot->error_value())
                           : active_slot.FormatDescription().data());
    return zx::error(active_slot.ok() ? active_slot->error_value() : active_slot.status());
  }

  bool install_to_b = false;
  const char* source_slot;
  const char* install_slot;
  using BootSlot = fuchsia_acpi_chromeos::wire::BootSlot;
  switch (active_slot->value()->slot) {
    case BootSlot::kA:
      // If we booted from slot A, install to slot B.
      install_to_b = true;
      install_slot = kFirmwareRwBSection;
      source_slot = kFirmwareRwASection;
      break;
    case BootSlot::kB:
      // If we booted from slot B, install to slot A.
      install_to_b = false;
      install_slot = kFirmwareRwASection;
      source_slot = kFirmwareRwBSection;
      break;
    default:
      // TODO(fxbug.dev/81685): in this situation, we should update *both* A and B.
      // This would be the same as "futility update --mode recovery" on CrOS.
      ERROR("Cannot do an A/B firmware update from recovery firmware. Bailing out.\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // The "new" firmware image has the same content in slots A and B, so we can easily determine
  // whether or not there's anything new by just comparing the currently-active firmware image
  // with the same slot in the new firmware image.
  LOG("Checking to see if slot '%s' differs\n", source_slot);
  auto src_area = FindArea(source_slot);
  if (src_area == std::nullopt) {
    ERROR("Cannot find section '%s', so cannot do a firmware update.\n", source_slot);
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  auto needs_update = NeedsUpdate(new_image, src_area.value());
  if (needs_update.is_error()) {
    return needs_update.take_error();
  }
  if (!needs_update.value()) {
    LOG("Active firmware version is identical to the update, skipping firmware update.\n");
    return zx::ok();
  }

  LOG("Installing firmware update to slot %c\n", install_to_b ? 'B' : 'A');
  auto install_area = FindArea(install_slot);
  if (install_area == std::nullopt) {
    ERROR("Cannot find section '%s', so cannot do a firmware update.\n", install_slot);
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  zx::vmo to_install;
  zx_status_t status = zx::vmo::create(install_area->size, 0, &to_install);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  uint8_t* install_data = static_cast<uint8_t*>(new_image.start()) + install_area->offset;
  status = to_install.write(install_data, 0, install_area->size);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // TODO(fxbug.dev/81685): don't erase so aggressively. The flashmap component should implement a
  // "EraseAndWrite" call that will erase only required blocks.
  auto erase_result =
      flashmap_->Erase(fidl::StringView::FromExternal(install_area->name), 0, install_area->size);
  if (!erase_result.ok() || erase_result->is_error()) {
    ERROR("Erase failed\n");
    return zx::error(erase_result.ok() ? erase_result->error_value() : erase_result.status());
  }
  auto write_result = flashmap_->Write(fidl::StringView::FromExternal(install_area->name), 0,
                                       fuchsia_mem::wire::Buffer{
                                           .vmo = std::move(to_install),
                                           .size = install_area->size,
                                       });
  if (!write_result.ok() || write_result->is_error()) {
    ERROR("write failed\n");
    return zx::error(write_result.ok() ? write_result->error_value() : write_result.status());
  }

  // Verify that the write succeeded.
  needs_update = NeedsUpdate(new_image, install_area.value());
  if (needs_update.is_error()) {
    ERROR("Failed verifying state\n");
    return needs_update.take_error();
  }
  if (needs_update.value()) {
    ERROR("Firmware is not consistent after write.\n");
    return zx::error(ZX_ERR_IO);
  }

  auto set_result = fwparam_->Set(fuchsia_vboot::wire::Key::kTryNext, install_to_b);
  if (!set_result.ok() || set_result->is_error()) {
    ERROR("Failed while setting TryNext parameter: %s\n",
          set_result.ok() ? zx_status_get_string(set_result->error_value())
                          : set_result.FormatDescription().data());
    return zx::error(set_result.ok() ? set_result->error_value() : set_result.status());
  }

  // We set TryCount to zero to indicate a "successful boot".
  // Vboot will fall back to the previous slot under the following circumstances:
  // 1. previous boot used the same firmware slot.
  // 2. previous boot had result "TRYING" (indicating the OS didn't start).
  // 3. TryCount is 0.
  // Vboot will set the boot result to "TRYING" if TryCount > 0.
  // Since (1) will be false, as long as we set TryCount to 0 we will never fall back.
  // See
  // https://source.chromium.org/chromiumos/_/chromium/chromiumos/platform/vboot_reference/+/1a7c57ce7fa5aa1c8cdc6bffffbfe3f8dbece664:firmware/2lib/2misc.c;l=345;drc=51879dc24aea94851fc28ffc2f68cba1b58f3db8
  // for the vboot logic.
  auto set_result2 = fwparam_->Set(fuchsia_vboot::wire::Key::kTryCount, 0);
  if (!set_result2.ok() || set_result2->is_error()) {
    ERROR("Failed while setting TryCount parameter: %s\n",
          set_result2.ok() ? zx_status_get_string(set_result2->error_value())
                           : set_result2.FormatDescription().data());
    return zx::error(set_result2.ok() ? set_result2->error_value() : set_result2.status());
  }

  LOG("Successfully did a firmware update!\n");
  return zx::ok();
}

zx::status<> FlashmapPartitionClient::FullUpdate(fzl::VmoMapper& new_image) {
  // TODO(fxbug.dev/81685): implement this.
  return zx::ok();
}

bool FlashmapPartitionClient::NeedsFullUpdate(GoogleBinaryBlockHeader* cur_gbb,
                                              GoogleBinaryBlockHeader* new_gbb) {
  // Check the two images are compatible.
  uint8_t* cur_gbb_bytes = reinterpret_cast<uint8_t*>(cur_gbb);
  uint8_t* new_gbb_bytes = reinterpret_cast<uint8_t*>(new_gbb);

  // Differing root key sizes need an update.
  if (cur_gbb->rootkey_size != new_gbb->rootkey_size) {
    return true;
  }
  // Differing root keys need an update.
  if (memcmp(&cur_gbb_bytes[cur_gbb->rootkey_offset], &new_gbb_bytes[new_gbb->rootkey_offset],
             cur_gbb->rootkey_size) != 0) {
    return true;
  }

  // Differing recovery key sizes need an update.
  if (cur_gbb->recovery_key_size != new_gbb->recovery_key_size) {
    return true;
  }

  // Differing recovery keys need an update.
  if (memcmp(&cur_gbb_bytes[cur_gbb->recovery_key_offset],
             &new_gbb_bytes[new_gbb->recovery_key_offset], cur_gbb->recovery_key_size) != 0) {
    return true;
  }

  // Looks like we can do a AB update.
  return false;
}

std::optional<FlashmapArea> FlashmapPartitionClient::FindArea(const char* name) {
  for (auto& area : areas_) {
    if (area.name == name) {
      return area;
    }
  }
  return std::nullopt;
}

zx::status<bool> FlashmapPartitionClient::NeedsUpdate(const fzl::VmoMapper& new_image,
                                                      const FlashmapArea& region) {
  auto result = flashmap_->Read(fidl::StringView::FromExternal(region.name), 0, region.size);
  if (!result.ok() || result->is_error()) {
    ERROR("Failed to read section '%s': %s\n", region.name.data(),
          result.ok() ? zx_status_get_string(result->error_value())
                      : result.FormatDescription().data());
    return zx::error(result.ok() ? result->error_value() : result.status());
  }

  auto& range = result->value()->range;
  fzl::VmoMapper cur_section;
  zx_status_t status = cur_section.Map(range.vmo);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  uint8_t* cur_section_p = static_cast<uint8_t*>(cur_section.start()) + range.offset;
  uint8_t* new_section_p = static_cast<uint8_t*>(new_image.start()) + region.offset;

  if (range.size != region.size) {
    ERROR("Area on flash did not match area in memory.\n");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (memcmp(cur_section_p, new_section_p, region.size) == 0) {
    LOG("Region '%s' is identical between new and old.\n", region.name.data());
    return zx::ok(false);
  }

  LOG("Region '%s' is not identical between new and old.\n", region.name.data());
  return zx::ok(true);
}

bool FlashmapPartitionClient::IsHWIDCompatible(const GoogleBinaryBlockHeader* cur_gbb,
                                               const GoogleBinaryBlockHeader* new_gbb) {
  // HWID is of the format <BOARDNAME> <NUMBERS AND LETTERS>.
  // In the firmware image, it is <BOARDNAME> TEST <HEX>.
  const char* cur_hwid = GetHWID(cur_gbb);
  const char* new_hwid = GetHWID(new_gbb);

  const char* cur_first_space = strchr(cur_hwid, ' ');
  const char* new_first_space = strchr(new_hwid, ' ');

  size_t cur_len = cur_first_space - cur_hwid;
  size_t new_len = new_first_space - new_hwid;

  if (cur_len != new_len) {
    return false;
  }

  return strncmp(cur_hwid, new_hwid, cur_len) == 0;
}

}  // namespace paver
