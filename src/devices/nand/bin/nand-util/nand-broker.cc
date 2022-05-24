// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand-broker.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/nand/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <new>

#include <pretty/hexdump.h>

namespace {

// Open a device named "broker" from the path provided. Fails if there is no
// device after 5 seconds.
fbl::unique_fd OpenBroker(const char* path) {
  fbl::unique_fd broker;

  auto callback = [](int dir_fd, int event, const char* filename, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE || strcmp(filename, "broker") != 0) {
      return ZX_OK;
    }
    fbl::unique_fd* broker = reinterpret_cast<fbl::unique_fd*>(cookie);
    broker->reset(openat(dir_fd, filename, O_RDWR));
    return ZX_ERR_STOP;
  };

  fbl::unique_fd dir(open(path, O_DIRECTORY));
  if (dir) {
    zx_time_t deadline = zx_deadline_after(ZX_SEC(5));
    fdio_watch_directory(dir.get(), callback, deadline, &broker);
  }
  return broker;
}

}  // namespace.

NandBroker::NandBroker(const char* path) : path_(path), device_(open(path, O_RDWR)) {}

bool NandBroker::Initialize() {
  if (!LoadBroker()) {
    return false;
  }
  zx_status_t status = fdio_get_service_handle(device_.release(), caller_.reset_and_get_address());
  if (status != ZX_OK) {
    printf("Failed to get device handle: %s\n", zx_status_get_string(status));
    return false;
  }

  if (!Query()) {
    printf("Failed to open or query the device\n");
    return false;
  }
  const uint32_t size = (info_.page_size + info_.oob_size) * info_.pages_per_block;
  if (mapping_.CreateAndMap(size, "nand-broker-vmo") != ZX_OK) {
    printf("Failed to allocate VMO\n");
    return false;
  }
  return true;
}

void NandBroker::SetFtl(std::unique_ptr<FtlInfo> ftl) { ftl_ = std::move(ftl); }

bool NandBroker::Query() {
  if (!caller_) {
    return false;
  }

  zx_status_t status;
  return fuchsia_nand_BrokerGetInfo(channel(), &status, &info_) == ZX_OK && status == ZX_OK;
}

void NandBroker::ShowInfo() const {
  printf(
      "Page size: %d\nPages per block: %d\nTotal Blocks: %d\nOOB size: %d\nECC bits: %d\n"
      "Nand class: %d\n",
      info_.page_size, info_.pages_per_block, info_.num_blocks, info_.oob_size, info_.ecc_bits,
      info_.nand_class);
}

bool NandBroker::ReadPages(uint32_t first_page, uint32_t count) const {
  ZX_DEBUG_ASSERT(count <= info_.pages_per_block);
  fuchsia_nand_BrokerRequestData request = {};

  request.length = count;
  request.offset_nand = first_page;
  request.offset_oob_vmo = info_.pages_per_block;  // OOB is at the end of the VMO.
  request.data_vmo = true;
  request.oob_vmo = true;

  zx::vmo vmo;
  if (mapping_.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo) != ZX_OK) {
    printf("Failed to duplicate VMO\n");
    return false;
  }
  request.vmo = vmo.release();

  zx_status_t status;
  uint32_t bit_flips;
  zx_status_t io_status = fuchsia_nand_BrokerRead(channel(), &request, &status, &bit_flips);
  if (io_status != ZX_OK) {
    printf("Failed to issue command to driver: %s\n", zx_status_get_string(io_status));
    return false;
  }

  if (status != ZX_OK) {
    printf("Read to %d pages starting at %d failed with %s\n", count, first_page,
           zx_status_get_string(status));
    return false;
  }

  if (bit_flips > info_.ecc_bits) {
    printf("Read to %d pages starting at %d unable to correct all bit flips\n", count, first_page);
  } else if (bit_flips) {
    // If the nand protocol is modified to provide more info, we could display something
    // like average bit flips.
    printf("Read to %d pages starting at %d corrected %d errors\n", count, first_page, bit_flips);
  }

  return true;
}

bool NandBroker::DumpPage(uint32_t page) const {
  if (!ReadPages(page, 1)) {
    return false;
  }
  ZX_DEBUG_ASSERT(info_.page_size % 16 == 0);

  uint32_t address = page * info_.page_size;
  hexdump8_ex(data(), 16, address);
  int skip = 0;

  for (uint32_t line = 16; line < info_.page_size; line += 16) {
    if (memcmp(data() + line, data() + line - 16, 16) == 0) {
      skip++;
      if (skip < 50) {
        printf(".");
      }
      continue;
    }
    if (skip) {
      printf("\n");
      skip = 0;
    }
    hexdump8_ex(data() + line, 16, address + line);
  }

  if (skip) {
    printf("\n");
  }

  printf("OOB:\n");
  hexdump8_ex(oob(), info_.oob_size, address + info_.page_size);
  return true;
}

bool NandBroker::EraseBlock(uint32_t block) const {
  fuchsia_nand_BrokerRequestData request = {};
  request.length = 1;
  request.offset_nand = block;

  zx_status_t status;
  zx_status_t io_status = fuchsia_nand_BrokerErase(channel(), &request, &status);
  if (io_status != ZX_OK) {
    printf("Failed to issue erase command for block %d: %s\n", block,
           zx_status_get_string(io_status));
    return false;
  }

  if (status != ZX_OK) {
    printf("Erase block %d failed with %s\n", block, zx_status_get_string(status));
    return false;
  }

  return true;
}

bool NandBroker::LoadBroker() {
  ZX_ASSERT(path_);
  if (strstr(path_, "/broker") == path_ + strlen(path_) - strlen("/broker")) {
    // The passed-in device is already a broker.
    return true;
  }

  // A broker driver may or may not be loaded. Try to load it and see if that
  // fails.
  fdio_t* io = fdio_unsafe_fd_to_io(device_.get());
  if (io == nullptr) {
    printf("Could not convert fd to io\n");
    return false;
  }
  zx_status_t call_status = ZX_OK;
  const char kBroker[] = "/boot/driver/nand-broker.so";
  auto resp = fidl::WireCall<fuchsia_device::Controller>(
                  zx::unowned_channel(fdio_unsafe_borrow_channel(io)))
                  ->Bind(::fidl::StringView(kBroker));
  auto status = resp.status();
  if (resp.Unwrap_NEW()->is_error()) {
    call_status = resp.Unwrap_NEW()->error_value();
  }

  fdio_unsafe_release(io);
  bool bind_failed = (status != ZX_OK || call_status != ZX_OK);

  device_ = OpenBroker(path_);
  if (!device_) {
    if (bind_failed) {
      printf("Failed to issue bind command for broker\n");
    } else {
      printf("Failed to bind broker\n");
    }
    return false;
  }
  return true;
}
