// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/disk-inspector/disk-inspector.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <zircon/status.h>

#include <blobfs/inspector/inspector.h>
#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <fbl/unique_fd.h>
#include <minfs/inspector.h>

namespace {

enum class FsType {
  kMinfs,
  kBlobfs,
};

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

int Inspect(std::unique_ptr<block_client::BlockDevice> device, FsType fs_type) {
  std::unique_ptr<disk_inspector::DiskObject> root;
  zx_status_t status;
  switch (fs_type) {
    case FsType::kMinfs: {
      minfs::Inspector inspector = minfs::Inspector(std::move(device));
      status = inspector.GetRoot(&root);
      break;
    }
    case FsType::kBlobfs: {
      blobfs::Inspector inspector = blobfs::Inspector(std::move(device));
      status = inspector.GetRoot(&root);
      break;
    }
    default: {
      status = ZX_ERR_NOT_SUPPORTED;
    }
  }
  if (status != ZX_OK) {
    fprintf(stderr, "ERROR: GetRoot failed\n");
    return -1;
  }
  ProcessDiskObjects(std::move(root), 0);
  printf("\n");
  return 0;
}

}  // namespace

int usage(const char *binary) {
  printf("usage: %s <device path> <--blobfs | --minfs>\n", binary);
  return -1;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    return usage(argv[0]);
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

  FsType fs_type;
  // TODO(fxb/37907): Disk-inspect should be more interactive and not depend on hackish flags
  // to function in the future.
  std::string flag = argv[2];
  if (flag == "--blobfs") {
    fs_type = FsType::kBlobfs;
  } else if (flag == "--minfs") {
    fs_type = FsType::kMinfs;
  } else {
    return usage(argv[0]);
  }

  return Inspect(std::move(device), fs_type);
}
