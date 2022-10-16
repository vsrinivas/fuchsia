// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "bootfs.h"

#include <lib/zbitl/error-stdio.h>
#include <stdarg.h>
#include <zircon/syscalls/log.h>
#include <zircon/types.h>

#include "util.h"

zx::vmo Bootfs::Open(std::string_view root, std::string_view filename, std::string_view purpose) {
  printl(log_, "searching BOOTFS for '%.*s%s%.*s' (%.*s)",    //
         static_cast<int>(root.size()), root.data(),          //
         root.empty() ? "" : "/",                             //
         static_cast<int>(filename.size()), filename.data(),  //
         static_cast<int>(purpose.size()), purpose.data());

  BootfsView bootfs = bootfs_reader_.root();
  auto it = root.empty() ? bootfs.find(filename) : bootfs.find({root, filename});
  if (auto result = bootfs.take_error(); result.is_error()) {
    Fail(result.error_value());
  }
  if (it == bootfs.end()) {
    fail(log_, "failed to find '%.*s%s%.*s'",         //
         static_cast<int>(root.size()), root.data(),  //
         root.empty() ? "" : "/",                     //
         static_cast<int>(filename.size()), filename.data());
  }

  // Clone a private, read-only snapshot of the file's subset of the bootfs VMO.
  zx::vmo file_vmo;
  zx_status_t status = bootfs_reader_.storage().vmo().create_child(
      ZX_VMO_CHILD_SNAPSHOT | ZX_VMO_CHILD_NO_WRITE, it->offset, it->size, &file_vmo);
  check(log_, status, "zx_vmo_create_child failed");

  status = file_vmo.set_property(ZX_PROP_NAME, filename.data(), filename.size());
  check(log_, status, "failed to set ZX_PROP_NAME");

  uint64_t size = it->size;
  status = file_vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size));
  check(log_, status, "failed to set ZX_PROP_VMO_CONTENT_SIZE");

  status = file_vmo.replace_as_executable(vmex_resource_, &file_vmo);
  check(log_, status, "zx_vmo_replace_as_executable failed");

  return file_vmo;
}

void Bootfs::Fail(const BootfsView::Error& error) {
  zbitl::PrintBootfsError(error, [this](const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printl(log_, fmt, args);
    va_end(args);
  });
  zx_process_exit(-1);
}
