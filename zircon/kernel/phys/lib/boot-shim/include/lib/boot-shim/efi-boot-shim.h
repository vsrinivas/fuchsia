// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_EFI_BOOT_SHIM_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_EFI_BOOT_SHIM_H_

#include <lib/acpi_lite.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <variant>

#include <efi/boot-services.h>
#include <efi/protocol/block-io.h>
#include <efi/protocol/file.h>
#include <efi/system-table.h>
#include <efi/types.h>

#include "acpi.h"
#include "boot-shim.h"
#include "efi.h"
#include "test-serial-number.h"

namespace boot_shim {

constexpr size_t kEfiPageSize = 4096;

// boot_shim::EfiBootShim<...> provides a template class (or base class)
// based on boot_shim::BootShim<...> for building UEFI-based boot shims.
// The shim always includes a standard suite of UEFI-based items, listed in
// the EfiBootShimBase definition below.  Template parameters can add more
// item types just like boot_shim::BootShim<...> template parameters.  The
// shim's Init() method can implicitly call Init() methods on these items;
// see below for details.  Those additional items might or might not use
// UEFI to collect their information.  Regardless, they should avoid
// anything like memory allocation or deallocation in their AppendItems
// methods so that the UEFI memory map remains stable.  Nonetheless, all
// items may have AppendItems called multiple times if it was necessary to
// discard the data from an earlier call; they should recapitulate the same
// data each time and not increase their size_bytes() results.

template <class... Items>
using EfiBootShimBase = BootShim<  //
    EfiSystemTableItem,            //
    EfiSmbiosItem,                 //
    AcpiUartItem,                  //
    AcpiRsdpItem,                  //
    TestSerialNumberItem,          //
    Items...>;

// This is common implementation code for the template class EfiBootShim<...>
// below.  It defines a few types used in the public API below.
class EfiBootShimLoader {
 public:
  using DataZbi = BootShimBase::DataZbi;
  using Error = DataZbi::Error;

  // LoadFunction is the type of the `load` callback passed to
  // ElfBootShim<...>::LoadAndBoot, see below.  It must load the kernel and
  // data ZBIs into memory and return the data ZBI with at least the requested
  // extra data capacity available on the end.
  using LoadResult = fit::result<Error, DataZbi>;
  using LoadFunction = fit::inline_function<LoadResult(uint32_t extra_data_capacity)>;

  // This is the type of the `last_chance` callback passed to
  // ElfBootShim<...>::LoadAndBoot, see below.  It is the last chance to
  // prevent the boot or to do anything using UEFI calls of any kind, such as
  // logging.  It gets the final DataZbi that the `boot` callback should use.
  using LastChanceFunction = fit::inline_function<fit::result<Error>(DataZbi)>;

  // This is the type of the `boot` callback passed to
  // ElfBootShim<...>::LoadAndBoot, see below.  It must not return, but
  // [[noreturn]] can't be applied to a type.
  using BootFunction = fit::inline_function<void()>;

  // The rest is implementation details used by ElfBootShim<...>::LoadAndBoot.

  // GetMemoryMap (below) returns this on success.
  struct MemoryMapInfo {
    cpp20::span<std::byte> map;  // Subset of the input buffer.
    size_t entry_size;           // Size of each entry.
    size_t key;                  // Map key to pass to ExitBootServices.
  };

  // The error value is the UEFI GetMemoryMap size required.
  using GetMemoryMapResult = fit::result<size_t, MemoryMapInfo>;

  using AppendItemsFunction = fit::function<fit::result<Error>(DataZbi&)>;

  static std::optional<acpi_lite::AcpiParser> GetAcpi(efi_system_table* systab,
                                                      const char* shim_name, FILE* log);

  static Error LoadAndBoot(efi_boot_services* boot_services, efi_handle image_handle,
                           LoadFunction load, LastChanceFunction last_chance, BootFunction boot,
                           size_t items_size, AppendItemsFunction append_items, const char* name,
                           FILE* log);

  // These are only used inside the implementation of LoadAndBoot, but are made
  // public for unit testing.

  // Call the UEFI GetMemoryMap function.
  static GetMemoryMapResult GetMemoryMap(efi_boot_services* boot_services,
                                         cpp20::span<std::byte> buffer);

  // Take a buffer filled by UEFI GetMemoryMap and convert in place into
  // ZBI_TYPE_MEM_CONFIG format.  The returned mem_config will reuse a leading
  // subspan of the buffer.
  static cpp20::span<zbi_mem_range_t> ConvertMemoryMap(cpp20::span<std::byte> buffer,
                                                       size_t entry_size);
};

// This has the boot_shim::BootShim<...> API with additional features.  It
// implicitly includes the EFI-based items listed above as well as any others
// given in the Items... list, for use with Get<...> et al.  It also implicitly
// provides a special ZBI_TYPE_MEM_CONFIG item generated from EFI data, but not
// part of the usual per-item API.  Instead, that item is handled specially as
// part of the LoadAndBoot() method, described below.
template <class... Items>
class EfiBootShim : public EfiBootShimBase<Items...> {
 public:
  using Base = EfiBootShimBase<Items...>;
  using BootFunction = EfiBootShimLoader::BootFunction;
  using LastChanceFunction = EfiBootShimLoader::LastChanceFunction;
  using LoadFunction = EfiBootShimLoader::LoadFunction;
  using LoadResult = EfiBootShimLoader::LoadResult;
  using typename BootShimBase::DataZbi;
  using Error = typename DataZbi::Error;

  explicit EfiBootShim(const char* name, FILE* log = stdout) : Base(name, log) {}

  using Base::Check;

  template <typename... T>
  bool Check(const char* what, const fit::result<efi_status, T...>& result) const {
    if (result.is_error()) {
      // TODO(mcgrathr): EFI error strings
      constexpr std::string_view kEfiError = "EFI error";
      return Check(what, fit::error{kEfiError});
    }
    return true;
  }

  // Initialize the shim items using the UEFI System Table.  This
  // initializes all the standard item types from EfiBootShimBase.
  // Each of Items... may have one of these methods:
  //  * void Init(efi_system_table* systab, const char* shim_name, FILE* log);
  //  * fit::result<efi_status> Init(efi_system_table* systab,
  //                                  const char* shim_name, FILE* log);
  //  * fit::result<Error> Init(efi_system_table* systab,
  //                             const char* shim_name, FILE* log);
  // Each item that does will have its method called here.  The caller of this
  // Init() method is responsible for initializing any items that use different
  // Init() signatures.
  fit::result<Error> Init(efi_system_table* systab) {
    // Set up ACPI access first.  Individual item setup will use it.
    acpi_ = EfiBootShimLoader::GetAcpi(systab, this->shim_name(), this->log());

    // Now call the Init() method on each item that takes either
    // efi_system_table* or AcpiParserInterface&.
    fit::result<Error> result = fit::ok();
    this->EveryItem([&](auto& item) {
      // The DoInit overloads below handle item.Init(...) signatures that take
      // the systab or the acpi() along with shim_name and log.  TryInitItem
      // handles the cases where Init(...)  returns void, result<Error>, or
      // result<efi_status>.
      result = TryInitItem(item, systab);
      return result.is_ok();
    });

    return result;
  }

  // If Init() found ACPI tables via the UEFI tables, this will be set.
  const std::optional<acpi_lite::AcpiParser>& acpi() const { return acpi_; }

  // This manages the entire loading and booting sequence via callbacks.
  // It only returns for error cases.
  //
  // First, it loads and split the input ZBI and prepare the data ZBI with shim
  // items.  The load function is called with the extra data capacity to leave
  // in the DataZbi for shim items.  It may be called multiple times if the
  // capacity has to be increased.  It should return the DataZbi container
  // loaded from the input ZBI with at least that much capacity remaining.
  // This will then append all the shim items, and call ExitBootServices()
  // immediately after calling last_chance().  This is final opportunity to use
  // UEFI Boot Services and prevent booting; it's the place for final logging.
  // After ExitBootServices succeeds, boot(zbi) is called with the final
  // location of the data ZBI, and must not return.  It should immediately boot
  // into the new kernel without doing anything that might attempt to use UEFI
  // calls.  (This is why it's done via callback rather than returning to the
  // caller, where many destructors would ordinarily run.)
  Error LoadAndBoot(efi_boot_services* boot_services, efi_handle image_handle, LoadFunction load,
                    LastChanceFunction last_chance, BootFunction boot) {
    return EfiBootShimLoader::LoadAndBoot(
        boot_services, image_handle, std::move(load), std::move(last_chance), std::move(boot),
        this->size_bytes(), [this](DataZbi& zbi) { return this->AppendItems(zbi); },
        this->shim_name(), this->log());
  }

 private:
  // This overload will be rejected by SFINAE if item.Init(systab, name, log)
  // isn't well-formed.
  template <class Item>
  auto DoInitItem(Item& item, efi_system_table* systab)
      -> decltype(item.Init(systab, this->shim_name(), this->log())) {
    return item.Init(systab, this->shim_name(), this->log());
  }

  // This overload will be rejected by SFINAE if item.Init(acpi, name, log)
  // isn't well-formed.
  template <class Item>
  auto DoInitItem(Item& item, efi_system_table* systab)
      -> decltype(item.Init(this->acpi(), this->shim_name(), this->log())) {
    return item.Init(this->acpi(), this->shim_name(), this->log());
  }

  // This will be the overload of last resort if SFINAE rejects the others.
  template <class Item>
  void DoInitItem(Item&, ...) {}

  // This calls one of those methods and handles its return value, if any.
  template <typename Item>
  fit::result<Error> TryInitItem(Item& item, efi_system_table* systab) {
    using Result = decltype(DoInitItem(item, systab));
    if constexpr (std::is_void_v<Result>) {
      DoInitItem(item, systab);
    } else if constexpr (std::is_convertible_v<Result, fit::result<efi_status>>) {
      fit::result<efi_status> result = DoInitItem(item, systab);
      if (result.is_error()) {
        // TODO(mcgrathr): EFI error strings
        fprintf(this->log(), "%s: EFI error %#zx\n", this->shim_name, result.error_value());
        return fit::error{Error{.zbi_error = "EFI error"}};
      }
    } else {
      static_assert(std::is_convertible_v<Result, fit::result<Error>>,
                    "Init(efi_system_table*, name, log) or "
                    "Init(AcpiParserInterface&, name, log) must return "
                    "void, fit::result<DataZbi::Error>, or fit::result<efi_error>");
      return DoInitItem(item, systab);
    }
    return fit::ok();
  }

  std::optional<acpi_lite::AcpiParser> acpi_;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_EFI_BOOT_SHIM_H_
