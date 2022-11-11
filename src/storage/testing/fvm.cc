// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/testing/fvm.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/unique_fd.h>
#include <ramdevice-client/ramdisk.h>

#include "src/lib/storage/fs_management/cpp/fvm.h"

namespace storage {

constexpr uuid::Uuid kTestPartGUID = {0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                      0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

constexpr uuid::Uuid kTestUniqueGUID = {0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

zx::result<> BindFvm(fidl::UnownedClientEnd<fuchsia_device::Controller> device) {
  auto resp = fidl::WireCall(device)->Bind("fvm.so");
  auto status = zx::make_result(resp.status());
  if (status.is_ok()) {
    if (resp->is_error()) {
      status = zx::make_result(resp->error_value());
    }
  }
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not bind disk to FVM driver";
    return status.take_error();
  }
  return zx::ok();
}

zx::result<std::string> CreateFvmInstance(const std::string& device_path, size_t slice_size) {
  zx::result device = component::Connect<fuchsia_hardware_block::Block>(device_path.c_str());
  if (device.is_error()) {
    return device.take_error();
  }
  auto status = zx::make_result(fs_management::FvmInit(device.value(), slice_size));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not format disk with FVM";
    return status.take_error();
  }
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  status = BindFvm(
      fidl::UnownedClientEnd<fuchsia_device::Controller>(device.value().borrow().channel()));
  if (status.is_error())
    return status.take_error();
  std::string fvm_disk_path = device_path + "/fvm";
  status = zx::make_result(wait_for_device(fvm_disk_path.c_str(), zx::sec(3).get()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "FVM driver never appeared at " << fvm_disk_path;
    return status.take_error();
  }

  return zx::ok(fvm_disk_path);
}

zx::result<std::string> CreateFvmPartition(const std::string& device_path, size_t slice_size,
                                           const FvmOptions& options) {
  if (options.name.size() >= BLOCK_NAME_LEN)
    return zx::error(ZX_ERR_INVALID_ARGS);

  // Format the raw device to support FVM, and bind the FVM driver to it.
  zx::result<std::string> fvm_disk_path_or = CreateFvmInstance(device_path, slice_size);
  if (fvm_disk_path_or.is_error()) {
    return fvm_disk_path_or.take_error();
  }

  // Open "fvm" driver
  auto fvm_fd = fbl::unique_fd(open(fvm_disk_path_or->c_str(), O_RDWR));
  if (!fvm_fd) {
    FX_LOGS(ERROR) << "Could not open FVM driver: errno=" << errno;
    return zx::error(ZX_ERR_BAD_STATE);
  }

  alloc_req_t request = {.slice_count = options.initial_fvm_slice_count};
  memcpy(request.name, options.name.data(), options.name.size());
  request.name[options.name.size()] = 0;
  memcpy(request.type, options.type ? options.type->data() : kTestPartGUID.bytes(),
         sizeof(request.type));
  memcpy(request.guid, kTestUniqueGUID.bytes(), sizeof(request.guid));

  if (auto fd_or = fs_management::FvmAllocatePartition(fvm_fd.get(), request); fd_or.is_error()) {
    FX_LOGS(ERROR) << "Could not allocate FVM partition (slice count: "
                   << options.initial_fvm_slice_count << ")";
    return fd_or.take_error();
  }
  fvm_fd.reset();

  std::string partition_path;
  fs_management::PartitionMatcher matcher{
      .type_guids = {uuid::Uuid(request.type)},
      .instance_guids = {kTestUniqueGUID},
  };
  if (auto fd_or = fs_management::OpenPartition(matcher, 0, &partition_path); fd_or.is_error()) {
    FX_LOGS(ERROR) << "Could not locate FVM partition";
    return fd_or.take_error();
  }
  return zx::ok(partition_path);
}

}  // namespace storage
