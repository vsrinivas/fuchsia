// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/efi-boot-shim.h>
#include <zircon/assert.h>

#include <algorithm>

#include <efi/boot-services.h>

namespace boot_shim {
namespace {

constexpr uint32_t EfiMemoryTypeToZbiMemRangeType(efi_memory_type type) {
  switch (type) {
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory:
      return ZBI_MEM_RANGE_RAM;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
      return ZBI_MEM_RANGE_PERIPHERAL;
    default:
      return ZBI_MEM_RANGE_RESERVED;
  }
}

constexpr zbi_mem_range_t EfiMemoryDescriptorToZbiMemRange(const efi_memory_descriptor& desc) {
  const efi_memory_type type = static_cast<efi_memory_type>(desc.Type);
  return {
      .paddr = desc.PhysicalStart,
      .length = desc.NumberOfPages * kEfiPageSize,
      .type = EfiMemoryTypeToZbiMemRangeType(type),
  };
}

// Coalesce the new entry into the previous entry if the types match and the
// address ranges are contiguous.
bool CoalesceZbiMemRanges(zbi_mem_range_t& prev, const zbi_mem_range_t& next) {
  if (prev.type == next.type && prev.paddr + prev.length == next.paddr) {
    prev.length += next.length;
    return true;
  }
  return false;
}

constexpr bool RangeLess(const zbi_mem_range_t& a, const zbi_mem_range_t& b) {
  return a.paddr < b.paddr;
}

}  // namespace

EfiBootShimLoader::GetMemoryMapResult EfiBootShimLoader::GetMemoryMap(
    efi_boot_services* boot_services, cpp20::span<std::byte> buffer) {
  size_t size = buffer.size_bytes(), key = 0, entry_size = 0;
  uint32_t version = 0;
  efi_status status = boot_services->GetMemoryMap(
      &size, reinterpret_cast<efi_memory_descriptor*>(buffer.data()), &key, &entry_size, &version);
  if (status == EFI_BUFFER_TOO_SMALL) {
    return fit::error(size);
  }

  // No other errors should be possible.
  ZX_ASSERT_MSG(status == EFI_SUCCESS, "GetMemoryMap failed: %zx", status);

  ZX_ASSERT_MSG(version == EFI_MEMORY_DESCRIPTOR_VERSION, "version %" PRIu32, version);

  return fit::ok(MemoryMapInfo{
      .map = buffer.subspan(0, size),
      .entry_size = entry_size,
      .key = key,
  });
}

cpp20::span<zbi_mem_range_t> EfiBootShimLoader::ConvertMemoryMap(cpp20::span<std::byte> buffer,
                                                                 size_t entry_size) {
  // We'll convert the buffer in place from EFI format to ZBI format.
  static_assert(sizeof(efi_memory_descriptor) >= sizeof(zbi_mem_range_t));
  ZX_ASSERT_MSG(entry_size >= sizeof(efi_memory_descriptor), "entry_size %#zx", entry_size);

  cpp20::span<zbi_mem_range_t> ranges{
      reinterpret_cast<zbi_mem_range_t*>(buffer.data()),
      buffer.size_bytes() / sizeof(zbi_mem_range_t),
  };

  auto next_zbi_range = ranges.begin();
  auto add_zbi_range = [&next_zbi_range, &ranges](zbi_mem_range_t entry) {
    ZX_DEBUG_ASSERT(next_zbi_range != ranges.end());
    if (next_zbi_range == ranges.begin() ||
        !CoalesceZbiMemRanges(*std::prev(next_zbi_range), entry)) {
      *next_zbi_range++ = entry;
    }
  };
  auto resize = [&next_zbi_range, &ranges]() {
    ranges = ranges.subspan(0, next_zbi_range - ranges.begin());
  };

  auto get_efi_range = [entry_size, buffer]() mutable {
    std::optional<efi_memory_descriptor> result;
    if (buffer.size_bytes() >= entry_size) {
      result = *reinterpret_cast<const efi_memory_descriptor*>(buffer.data());
      buffer = buffer.subspan(entry_size);
    }
    return result;
  };

  while (auto desc = get_efi_range()) {
    // Ignore useless zero-length entries; UEFI sometimes generates a lot.
    if (desc->NumberOfPages > 0) {
      add_zbi_range(EfiMemoryDescriptorToZbiMemRange(*desc));
    }
  }
  resize();

  // Sort the ranges by address, and then re-coalesce.  This isn't required,
  // but it makes for a short and tidy payload.
  std::sort(ranges.begin(), ranges.end(), RangeLess);
  next_zbi_range = ranges.begin();
  std::for_each(ranges.begin(), ranges.end(), add_zbi_range);
  resize();

  return ranges;
}

// This is called by EfiBootShim<...>::Init().
// Errors from ACPI are logged but don't prevent Init() from succeeding.
std::optional<acpi_lite::AcpiParser> EfiBootShimLoader ::GetAcpi(efi_system_table* systab,
                                                                 const char* shim_name, FILE* log) {
  auto acpi = EfiGetAcpi(systab);
  if (acpi.is_error()) {
    const char* error = "unexpected error";
    switch (acpi.status_value()) {
      case ZX_ERR_NOT_FOUND:
        error = "not present";
        break;
      case ZX_ERR_IO_DATA_INTEGRITY:
        error = "corrupted tables";
        break;
      case ZX_ERR_NOT_SUPPORTED:
        error = "unsupported version";
        break;
    }
    fprintf(log, "%s: Cannot find ACPI tables from EFI: %s\n", shim_name, error);
    return std::nullopt;
  }

  return std::move(acpi).value();
}

EfiBootShimLoader::Error EfiBootShimLoader::LoadAndBoot(efi_boot_services* boot_services,
                                                        efi_handle image_handle, LoadFunction load,
                                                        LastChanceFunction last_chance,
                                                        BootFunction boot, size_t items_size,
                                                        AppendItemsFunction append_items,
                                                        const char* shim_name, FILE* log) {
  // First get an initial estimate of the memory map size as it is.
  size_t memory_map_size = [boot_services]() -> size_t {
    auto result = GetMemoryMap(boot_services, {});
    ZX_ASSERT_MSG(result.is_error(), "GetMemoryMap returned EFI_SUCCESS with empty buffer!");
    return result.error_value();
  }();

  DataZbi zbi;
  DataZbi::iterator item;
  MemoryMapInfo info;
  auto get_map = [&](bool verbose) -> bool {
    auto result = GetMemoryMap(boot_services, item->payload);
    if (result.is_error()) {
      const size_t new_size = result.error_value();

      if (verbose) {
        fprintf(log, "%s: *** GetMemoryMap size grew from previous estimate %#zx to %#zx ***\n",
                shim_name, memory_map_size, new_size);
      }

      ZX_ASSERT_MSG(new_size > memory_map_size, "%#zx <= %#zx", new_size, memory_map_size);

      // Always increase the new size estimate, never decrease it.  In the
      // situation where this estimate triggers reallocating the data ZBI and
      // that allocation itself changes the memory map size, this should avoid
      // ever getting into an oscillation that fails to converge on a stable
      // memory map.
      memory_map_size = std::max(new_size, memory_map_size + sizeof(efi_memory_descriptor));

      // Repeat the whole exercise with a new size estimate.
      //
      // TODO(mcgrathr): This could just attempt to allocate a new block for
      // the data ZBI and copy into it, rather than re-loading everything.
      // That would need a more complicated callback API to maintain the
      // unit-testable layering here.
      return false;
    }
    info = result.value();
    return true;
  };

  do {
    // Compute the extra data capacity to request at the end of the data ZBI.
    // This will hold all the items from append_items (should be <= items_size
    // bytes total); and the ZBI_TYPE_MEM_CONFIG item synthesized here.  We get
    // the EFI memory map in place in the same storage and convert it in place.
    // So this capacity must be sufficient for the whole EFI memory map, even
    // though the ZBI_TYPE_MEM_CONFIG item payload is always smaller than the
    // original EFI memory map buffer.
    size_t capacity = items_size + sizeof(zbi_header_t) + memory_map_size;

    // The initial estimate is based on the memory map size as it is now,
    // before loading.  It's almost certain that the allocations for loading
    // will add new entries to the memory map.  So make this a generous
    // overestimate in hopes that the first allocation will be large enough.
    capacity += kEfiPageSize;

    // Attempt to load the ZBI into memory, split between kernel and data ZBI.
    if (auto result = load(static_cast<uint32_t>(capacity)); result.is_ok()) {
      zbi = result.value();
    } else {
      return result.error_value();
    }

    // The load function should have left at least as much space as requested.
    ZX_ASSERT(zbi.size_bytes() <= zbi.storage().size_bytes());
    size_t space = zbi.storage().size_bytes() - zbi.size_bytes();
    ZX_ASSERT_MSG(space >= capacity,
                  "data ZBI storage %#zx bytes ZBI %#zx bytes leaves %#zx capacity < %#zx required",
                  zbi.storage().size_bytes(), zbi.size_bytes(), space, capacity);

    // Now append the miscellaneous items.
    if (auto result = append_items(zbi); result.is_error()) {
      return result.error_value();
    }

    if (auto result = zbi.take_error(); result.is_error()) {
      return result.error_value();
    }

    ZX_ASSERT_MSG(zbi.size_bytes() <= zbi.storage().size_bytes(),
                  "AppendItems functions used too much space");
    space = zbi.storage().size_bytes() - zbi.size_bytes();
    ZX_ASSERT_MSG(space >= sizeof(zbi_header_t) + memory_map_size,
                  "AppendItems functions used too much space");
    space -= sizeof(zbi_header_t);

    // Use the rest of the available payload space as the buffer for the
    // GetMemoryMap call.
    if (auto result = zbi.Append({
            .type = ZBI_TYPE_MEM_CONFIG,
            .length = static_cast<uint32_t>(space),
        });
        result.is_ok()) {
      item = result.value();
    } else {
      return result.error_value();
    }

    ZX_ASSERT_MSG(item->payload.size_bytes() >= space, "%#zx < %#zx", item->payload.size_bytes(),
                  space);

    // Get the full map to be sure we can, though it may yet change.
    if (!get_map(false)) {
      continue;
    }

    // Make the final callback that can use UEFI Boot Services, e.g. logging.
    if (auto result = last_chance(zbi); result.is_error()) {
      return result.error_value();
    }

    // Even calling into the UEFI Simple Text Output Protocol can invalidate
    // the memory map, so always fetch it anew as the very last thing before
    // ExitBootServices with no possible intervening UEFI calls of any kind.
    // It's possible the map size grew, though it should not have.
  } while (!get_map(true));

  // Convert the memory map in place to ZBI format.
  cpp20::span payload = ConvertMemoryMap(info.map, info.entry_size);

  // That probably didn't use all the buffer space, so trim the item.
  const uint32_t payload_size = static_cast<uint32_t>(payload.size_bytes());
  auto result = zbi.TrimLastItem(item, payload_size);
  if (result.is_error()) {
    return result.error_value();
  }

  // Now attempt ExitBootServices.  This should always work the first time
  // since the map key cannot have been invalidated with no UEFI calls made.
  efi_status status = boot_services->ExitBootServices(image_handle, info.key);
  switch (status) {
    case EFI_SUCCESS:
      // We're ready to boot!  UEFI Boot Services are no longer available,
      // so there is no logging to be done any more.
      // TODO(mcgrathr): UEFI Runtime Services are still available here,
      // so this could be the latest possible chance to clear the crashlog.
      boot();

      // TODO(mcgrathr): no more UEFI output is available (probably), but
      // we could switch stdout to the phys uart driver (if we have one)
      // for final panic messages here or in the boot callback
      ZX_PANIC("boot callback returned!");
      while (true) {
        __builtin_trap();
      }

    case EFI_INVALID_PARAMETER:
      fprintf(log, "%s: ExitBootServices reported invalid map key %#zx\n", shim_name, info.key);
      return {.zbi_error = "map key invalidated before ExitBootServices!"};

    default:
      // The UEFI spec says EFI_INVALID_PARAMETER for the wrong map key is
      // the only error possible.
      fprintf(log, "%s: ExitBootServices got unexpected EFI error %#zx\n", shim_name, status);
      return {.zbi_error = "unexpected EFI error from ExitBootServices"};
  }
}

}  // namespace boot_shim
