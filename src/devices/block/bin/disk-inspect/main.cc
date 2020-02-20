// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <zircon/status.h>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <disk_inspector/disk_inspector.h>
#include <fbl/unique_fd.h>
#include <minfs/inspector.h>

namespace {

// Processes various disk objects recursively starting from root object
// and prints values/elements of the objects.
void ProcessDiskObjects(std::unique_ptr<disk_inspector::DiskObject> obj, uint32_t num_tabs) {
  if (obj == nullptr)
    return;

  printf("\n");
  for (uint32_t i = 0; i < num_tabs; i++) {
    printf("\t");
  }

  printf("Name: %-25s", obj->GetName());
  size_t num_elements = obj->GetNumElements();

  // Non scalar types.
  if (num_elements != 0) {
    for (uint32_t i = 0; i < num_elements; i++) {
      std::unique_ptr<disk_inspector::DiskObject> element = obj->GetElementAt(i);
      ProcessDiskObjects(std::move(element), num_tabs + 1);
    }
    return;
  }

  const void *buffer;
  size_t size;
  obj->GetValue(&buffer, &size);

  switch (size) {
    case sizeof(uint64_t): {
      const uint64_t *val = reinterpret_cast<const uint64_t *>(buffer);
      printf(" Value:0x%lx", *val);
    } break;

    case sizeof(uint32_t): {
      const uint32_t *val = reinterpret_cast<const uint32_t *>(buffer);
      printf(" Value:0x%x", *val);
    } break;

    case sizeof(uint16_t): {
      const uint16_t *val = reinterpret_cast<const uint16_t *>(buffer);
      printf(" Value:0x%x", *val);
    } break;

    case sizeof(uint8_t): {
      const char *val = reinterpret_cast<const char *>(buffer);
      printf(" Value:%c", *val);
    } break;

    default:
      ZX_ASSERT_MSG(false, "Unknown object size: %lu\n", size);
  }
}

int Inspect(std::unique_ptr<block_client::BlockDevice> device) {
  minfs::Inspector inspector = minfs::Inspector(std::move(device));
  std::unique_ptr<disk_inspector::DiskObject> root;

  if (inspector.GetRoot(&root) == ZX_OK) {
    ProcessDiskObjects(std::move(root), 0);
    printf("\n");
    return 0;
  }
  fprintf(stderr, "ERROR: GetRoot failed\n");
  return -1;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <device path>\n", argv[0]);
    return -1;
  }

  fbl::unique_fd fd(open(argv[1], O_RDONLY));
  if (fd.get() < 0) {
    fprintf(stderr, "ERROR: Failed to open device: %d\n", fd.get());
    return -1;
  }

  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "ERROR: cannot acquire handle: %d\n", status);
    return -1;
  }

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  status = block_client::RemoteBlockDevice::Create(std::move(channel), &device);
  if (status != ZX_OK) {
    fprintf(stderr, "ERROR: cannot create remote device: %d\n", status);
    return -1;
  }

  return Inspect(std::move(device));
}
