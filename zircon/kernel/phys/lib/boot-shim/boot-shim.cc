// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/boot-shim.h>
#include <lib/zbitl/error-stdio.h>
#include <stdio.h>

#include <pretty/cpp/sizes.h>

namespace boot_shim {

bool BootShimBase::Check(const char* what, fit::result<std::string_view> result) const {
  if (result.is_error()) {
    fprintf(log_, "%s: %s: %.*s", shim_name_, what, static_cast<int>(result.error_value().size()),
            result.error_value().data());
    return false;
  }
  return true;
}

bool BootShimBase::Check(const char* what, fit::result<InputZbi::Error> result) const {
  if (result.is_error()) {
    fprintf(log_, "%s: %s: ", shim_name_, what);
    zbitl::PrintViewError(result.error_value(), log_);
    return false;
  }
  return true;
}

bool BootShimBase::Check(const char* what,
                         fit::result<InputZbi::CopyError<WritableBytes>> result) const {
  if (result.is_error()) {
    fprintf(log_, "%s: %s: ", shim_name_, what);
    zbitl::PrintViewCopyError(result.error_value(), log_);
    return false;
  }
  return true;
}

bool BootShimBase::Check(const char* what, fit::result<DataZbi::Error> result) const {
  if (result.is_error()) {
    fprintf(log_, "%s: %s: ", shim_name_, what);
    zbitl::PrintViewError(result.error_value(), log_);
    return false;
  }
  return true;
}

void BootShimBase::Log(const Cmdline& cmdline_item, ByteView ramdisk) const {
  std::string_view boot_loader = cmdline_item[Cmdline::kInfo];
  std::string_view cmdline = cmdline_item[Cmdline::kLegacy];

  if (boot_loader.empty()) {
    boot_loader = "unknown legacy boot loader";
  }

  fprintf(log_, "%s: Legacy boot from %.*s.\n", shim_name_, static_cast<int>(boot_loader.size()),
          boot_loader.data());

  if (cmdline.empty()) {
    fprintf(log_, "%s: No command line from legacy boot loader!\n", shim_name_);
  } else {
    const void* start = cmdline.data();
    const void* end = cmdline.data() + cmdline.size();
    fprintf(log_, "%s:   CMDLINE @ [%p, %p): %.*s\n", shim_name_, start, end,
            static_cast<int>(cmdline.size()), cmdline.data());
  }

  if (ramdisk.empty()) {
    fprintf(log_, "%s: Missing or empty RAMDISK: No ZBI!\n", shim_name_);
  } else {
    const void* start = ramdisk.data();
    const void* end = ramdisk.data() + ramdisk.size();
    fprintf(log_, "%s:   RAMDISK @ [%p, %p): %s from legacy boot loader\n", shim_name_, start, end,
            pretty::FormattedBytes(ramdisk.size()).c_str());
  }
}

}  // namespace boot_shim
