// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <ctype.h>
#include <lib/boot-options/word-view.h>
#include <lib/stdcompat/string_view.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <optional>
#include <string>
#include <string_view>

#include <fbl/algorithm.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "zircon/device/vfs.h"

namespace {

constexpr std::string_view kBootsvcNextArg = "bootsvc.next=";

// Returns true for boot item types that should be stored.
bool StoreItem(uint32_t type) {
  switch (type) {
    case ZBI_TYPE_CMDLINE:
      return true;
    default:
      return false;
  }
}

// Discards the boot item from the boot image VMO.
void DiscardItem(zx::vmo* vmo, uint32_t begin_in, uint32_t end_in) {
  uint64_t begin = fbl::round_up(begin_in, static_cast<uint32_t>(zx_system_get_page_size()));
  uint64_t end = fbl::round_down(end_in, static_cast<uint32_t>(zx_system_get_page_size()));
  if (begin < end) {
    printf(
        "bootsvc: Would have decommitted BOOTDATA VMO from %#lx to %#lx, but deferring to "
        "component manager's ZBI parser instead\n",
        begin, end);
  }
}

bootsvc::ItemKey CreateItemKey(uint32_t type, uint32_t extra) {
  return bootsvc::ItemKey{.type = type, .extra = extra};
}

bootsvc::ItemValue CreateItemValue(uint32_t type, uint32_t off, uint32_t len) {
  off = safemath::CheckAdd(off, sizeof(zbi_header_t)).ValueOrDie<uint32_t>();
  return bootsvc::ItemValue{.offset = off, .length = len};
}

}  // namespace

namespace bootsvc {

const char* const kLastPanicFilePath = "log/last-panic.txt";

zx_status_t RetrieveBootImage(zx::vmo vmo, zx::vmo* out_vmo, ItemMap* out_map,
                              BootloaderFileMap* out_bootloader_file_map) {
  // Validate boot image VMO provided by startup handle.
  zbi_header_t header;
  zx_status_t status = vmo.read(&header, 0, sizeof(header));
  if (status != ZX_OK) {
    printf("bootsvc: Failed to read ZBI image header: %s\n", zx_status_get_string(status));
    return status;
  } else if (header.type != ZBI_TYPE_CONTAINER || header.extra != ZBI_CONTAINER_MAGIC ||
             header.magic != ZBI_ITEM_MAGIC || !(header.flags & ZBI_FLAG_VERSION)) {
    printf("bootsvc: Invalid ZBI image header\n");
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Used to discard pages from the boot image VMO.
  uint32_t discard_begin = 0;
  uint32_t discard_end = 0;

  // Read boot items from the boot image VMO.
  ItemMap map;
  BootloaderFileMap bootloader_file_map;

  uint32_t off = sizeof(header);
  uint32_t len = header.length;
  while (len > sizeof(header)) {
    status = vmo.read(&header, off, sizeof(header));
    if (status != ZX_OK) {
      printf("bootsvc: Failed to read ZBI item header: %s\n", zx_status_get_string(status));
      return status;
    } else if (header.type == ZBI_CONTAINER_MAGIC || header.magic != ZBI_ITEM_MAGIC) {
      printf("bootsvc: Invalid ZBI item header\n");
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    uint32_t item_len = ZBI_ALIGN(header.length + static_cast<uint32_t>(sizeof(zbi_header_t)));
    uint32_t next_off = safemath::CheckAdd(off, item_len).ValueOrDie();

    if (item_len > len) {
      printf("bootsvc: ZBI item too large (%u > %u)\n", item_len, len);
      return ZX_ERR_IO_DATA_INTEGRITY;
    } else if (StoreItem(header.type)) {
      const ItemKey key = CreateItemKey(header.type, header.extra);
      const ItemValue val = CreateItemValue(header.type, off, header.length);

      auto [it, _] = map.emplace(key, std::vector<ItemValue>{});
      it->second.push_back(val);

      DiscardItem(&vmo, discard_begin, discard_end);
      discard_begin = next_off;
    } else {
      discard_end = next_off;
    }
    off = next_off;
    len = safemath::CheckSub(len, item_len).ValueOrDie();
  }

  if (discard_end > discard_begin) {
    // We are at the end of the last element and it should be discarded.
    // We should discard until the end of the page.
    discard_end = fbl::round_up(discard_end, static_cast<uint32_t>(zx_system_get_page_size()));
  }

  DiscardItem(&vmo, discard_begin, discard_end);
  *out_vmo = std::move(vmo);
  *out_map = std::move(map);
  *out_bootloader_file_map = std::move(bootloader_file_map);
  return ZX_OK;
}

std::optional<std::string> GetBootsvcNext(std::string_view str) {
  std::optional<std::string> next;
  for (std::string_view word : WordView(str)) {
    if (cpp20::starts_with(word, kBootsvcNextArg)) {
      next = word.substr(kBootsvcNextArg.size());
    }
  }

  return next;
}

zx_status_t CreateVnodeConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                                  fs::Rights rights, zx::channel* out) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }

  status = vfs->ServeDirectory(std::move(vnode), std::move(local), rights);
  if (status != ZX_OK) {
    return status;
  }
  *out = std::move(remote);
  return ZX_OK;
}

std::vector<std::string> SplitString(std::string_view input, char delimiter) {
  const bool ends_with_delimiter = !input.empty() && input.back() == delimiter;

  std::vector<std::string> result;
  while (!input.empty()) {
    std::string_view word = input.substr(0, input.find_first_of(delimiter));
    result.push_back(std::string{word});
    input.remove_prefix(std::min(input.size(), word.size() + sizeof(delimiter)));
  }

  if (ends_with_delimiter) {
    result.push_back("");
  }

  return result;
}

}  // namespace bootsvc
