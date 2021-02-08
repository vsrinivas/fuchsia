// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "boot-zbi.h"

#include <lib/arch/zbi-boot.h>

namespace {

constexpr fitx::error<BootZbi::Error> InputError(BootZbi::InputZbi::Error error) {
  return fitx::error{BootZbi::Error{
      .zbi_error = error.zbi_error,
      .read_offset = error.item_offset,
  }};
}

constexpr fitx::error<BootZbi::Error> EmptyZbi(fitx::result<BootZbi::InputZbi::Error> result) {
  if (result.is_error()) {
    return InputError(result.error_value());
  }
  return fitx::error{BootZbi::Error{"empty ZBI"}};
}

constexpr fitx::error<BootZbi::Error> OutputError(BootZbi::Zbi::Error error) {
  return fitx::error{BootZbi::Error{
      .zbi_error = error.zbi_error,
      .write_offset = error.item_offset,
  }};
}

constexpr fitx::error<BootZbi::Error> OutputError(
    BootZbi::InputZbi::CopyError<BootZbi::Bytes> error) {
  return fitx::error{BootZbi::Error{
      .zbi_error = error.zbi_error,
      .read_offset = error.read_offset,
      .write_offset = error.write_offset,
  }};
}

}  // namespace

BootZbi::Size BootZbi::SuggestedAllocation(uint32_t zbi_size_bytes) {
  return {.size = zbi_size_bytes, .alignment = arch::kZbiBootKernelAlignment};
}

fitx::result<BootZbi::Error, BootZbi::Sizes> BootZbi::GetSizes(InputZbi zbi) {
  const zbi_kernel_t* kernel;

  auto it = zbi.begin();
  if (it == zbi.end()) {
    return EmptyZbi(zbi.take_error());
  }
  if (auto [header, payload] = *it;
      header->type == arch::kZbiBootKernelType && payload.size() > sizeof(zbi_kernel_t)) {
    kernel = reinterpret_cast<const zbi_kernel_t*>(payload.data());
  } else {
    zbi.ignore_error();
    return fitx::error{Error{
        .zbi_error = "ZBI does not start with valid kernel item",
        .read_offset = it.item_offset(),
    }};
  }

  Sizes sizes = {
      .kernel = {.size = 0, .alignment = arch::kZbiBootKernelAlignment},
      .data = {.size = 0, .alignment = arch::kZbiBootDataAlignment},
  };

  ++it;
  if (auto result = zbi.take_error(); result.is_error()) {
    return InputError(result.error_value());
  }

  if (it == zbi.end()) {
    // No data items.
    sizes.kernel.size = zbi.size_bytes();
  } else {
    sizes.kernel.size = it.item_offset();
    sizes.data.size = zbi.size_bytes() - sizes.kernel.size;
  }

  // There must be a container header for the data ZBI even if it's empty.
  sizes.data.size += sizeof(zbi_header_t);

  // The kernel needs extra memory after its load image.
  sizes.kernel.size += kernel->reserve_memory_size;

  // If the incoming ZBI is already sufficiently aligned and has enough space
  // after the kernel (i.e. where the data items are) for the bss, reuse it.
  if (sizes.kernel.size <= zbi.size_bytes() &&
      reinterpret_cast<uintptr_t>(zbi.storage().data()) % arch::kZbiBootKernelAlignment == 0) {
    sizes.kernel.size = 0;
  }

  return fitx::ok(sizes);
}

fitx::result<BootZbi::Error> BootZbi::Load(InputZbi zbi) {
  auto it = zbi.begin();
  if (it == zbi.end()) {
    return EmptyZbi(zbi.take_error());
  }

  auto first = it;

  ++it;
  if (auto result = zbi.take_error(); result.is_error()) {
    return InputError(result.error_value());
  }

  if (kernel_.storage().empty()) {
    // The input ZBI is being used in place for the kernel.
    kernel_.storage() = {const_cast<std::byte*>(zbi.storage().data()), zbi.storage().size()};
  } else {
    if (auto result = kernel_.clear(); result.is_error()) {
      return OutputError(result.error_value());
    }
    if (auto result = kernel_.Extend(first, it); result.is_error()) {
      return result.take_error();
    }
  }

  if (auto result = data_.clear(); result.is_error()) {
    return OutputError(result.error_value());
  }
  if (auto result = data_.Extend(it, zbi.end()); result.is_error()) {
    return OutputError(result.error_value());
  }

  return fitx::ok();
}

[[noreturn]] void BootZbi::Boot() {
  arch::ZbiBoot(reinterpret_cast<zircon_kernel_t*>(kernel_.storage().data()),
                reinterpret_cast<zbi_header_t*>(data_.storage().data()));
}
