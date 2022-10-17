// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_FLASHMAP_CLIENT_H_
#define SRC_STORAGE_LIB_PAVER_FLASHMAP_CLIENT_H_

#include <fidl/fuchsia.acpi.chromeos/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.nand.flashmap/cpp/wire.h>
#include <fidl/fuchsia.vboot/cpp/wire.h>
#include <lib/fzl/vmo-mapper.h>

#include <fbl/unique_fd.h>

#include "src/storage/lib/paver/partition-client.h"
#include "third_party/vboot_reference/firmware/include/gbb_header.h"

namespace paver {
struct FlashmapArea {
  std::string name;
  uint32_t offset;
  uint32_t size;
  bool preserve;

  explicit FlashmapArea(fuchsia_nand_flashmap::wire::Area other)
      : name(other.name.data(), other.name.size()),
        offset(other.offset),
        size(other.size),
        preserve(other.flags & fuchsia_nand_flashmap::wire::AreaFlags::kPreserve) {}
};

// Partition client that reads and writes flashmap-formatted images.
// Note that we don't support substantially changing the flash layout, because the current
// implementation assumes that the new image has the same layout as the installed image.
class FlashmapPartitionClient : public PartitionClient {
 public:
  // Public for the benefit of std::make_unique. You probably want |FlashmapClient::Create|.
  explicit FlashmapPartitionClient(fidl::ClientEnd<fuchsia_nand_flashmap::Flashmap> flashmap,
                                   fidl::ClientEnd<fuchsia_acpi_chromeos::Device> cros_acpi,
                                   fidl::ClientEnd<fuchsia_vboot::FirmwareParam> fwparam)
      : flashmap_(std::move(flashmap)),
        cros_acpi_(std::move(cros_acpi)),
        fwparam_(std::move(fwparam)) {}

  static zx::result<std::unique_ptr<FlashmapPartitionClient>> Create(
      const fbl::unique_fd& devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root,
      zx::duration timeout);

  // Helper for creating partition client in tests.
  static zx::result<std::unique_ptr<FlashmapPartitionClient>> CreateWithClients(
      fidl::ClientEnd<fuchsia_nand_flashmap::Flashmap> flashmap,
      fidl::ClientEnd<fuchsia_acpi_chromeos::Device> cros_acpi,
      fidl::ClientEnd<fuchsia_vboot::FirmwareParam> fwparam);

  zx::result<size_t> GetBlockSize() final { return zx::ok(erase_block_size_); }
  zx::result<size_t> GetPartitionSize() final;
  zx::result<> Read(const zx::vmo& vmo, size_t size) final;
  zx::result<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::result<> Trim() final { return zx::ok(); }
  zx::result<> Flush() final { return zx::ok(); }
  fbl::unique_fd block_fd() final { return fbl::unique_fd(); }

 private:
  // Initialisation of state that might fail (i.e. getting area list and erase block size over
  // FIDL).
  zx::result<> Init();

  // Do a full update, overwriting RW and RO flash sections, except areas marked as PRESERVE. Also
  // preserve the HWID and GBB flags.
  zx::result<> FullUpdate(fzl::VmoMapper& new_image);
  // Do an A/B update, updating the inactive RW section only.
  zx::result<> ABUpdate(fzl::VmoMapper& new_image);

  // Compare the public keys stored in the GBB with the current GBB to determine if the new firmware
  // image needs a full update.
  bool NeedsFullUpdate(GoogleBinaryBlockHeader* cur_gbb, GoogleBinaryBlockHeader* new_gbb);

  // Returns true if the RO section of flash is writable.
  zx::result<bool> CanWriteRO();

  std::optional<FlashmapArea> FindArea(const char* name);
  // Returns true if the given region differs between |new_image| and |flashmap_|.
  zx::result<bool> NeedsUpdate(const fzl::VmoMapper& new_image, const FlashmapArea& region);

  // Returns true if the two GBBs have compatible HWIDs.
  // Note that we never overwrite HWID, but we use the first word ("ATLAS", "EVE", etc.) to
  // make sure that the firmware we're going to try installing is correct.
  bool IsHWIDCompatible(const GoogleBinaryBlockHeader* cur_gbb,
                        const GoogleBinaryBlockHeader* new_gbb);

  fidl::WireSyncClient<fuchsia_nand_flashmap::Flashmap> flashmap_;
  fidl::WireSyncClient<fuchsia_acpi_chromeos::Device> cros_acpi_;
  fidl::WireSyncClient<fuchsia_vboot::FirmwareParam> fwparam_;
  std::vector<FlashmapArea> areas_;
  uint32_t erase_block_size_ = 0;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_FLASHMAP_CLIENT_H_
