// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/skipblock/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/boot/bootfs.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <lz4/lz4frame.h>
#include <zbi-bootfs/zbi-bootfs.h>
#include <zstd/zstd.h>

#include "src/lib/bootfs/parser.h"

namespace zbi_bootfs {

const uint32_t kMaxDecompressedZbiSize = (1 << 30);  // 1 GiB

namespace {
zx_status_t FindEntry(zx::unowned_vmo vmo, const char* filename, zbi_bootfs_dirent_t* entry) {
  bootfs::Parser parser;
  zx_status_t status = parser.Init(std::move(vmo));
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to init bootfs::Parser: %s\n", zx_status_get_string(status));
    return status;
  }

  // TODO(joeljacob): Consider making the vector a class member
  // This will prevent unnecessarily re-reading the VMO
  fbl::Vector<const zbi_bootfs_dirent_t*> parsed_entries;
  parser.Parse([&](const zbi_bootfs_dirent_t* entry) {
    parsed_entries.push_back(entry);
    return ZX_OK;
  });

  for (const auto& parsed_entry : parsed_entries) {
    printf("Entry = %s\n ", parsed_entry->name);

    // This is not the entry we are looking for.
    if (strcmp(parsed_entry->name, filename) != 0) {
      continue;
    }

    printf("Filename = %s\n ", parsed_entry->name);
    printf("File name length = %d\n", parsed_entry->name_len);
    printf("File data length = %d\n", parsed_entry->data_len);
    printf("File data offset = %d\n", parsed_entry->data_off);

    memcpy(entry, parsed_entry, sizeof(zbi_bootfs_dirent_t));
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}
}  // namespace

bool ZbiBootfsParser::IsSkipBlock(const char* path,
                                  fuchsia_hardware_skipblock_PartitionInfo* partition_info) {
  fbl::unique_fd fd(open(path, O_RDONLY));
  if (!fd) {
    return false;
  }

  fdio_cpp::FdioCaller caller(std::move(fd));

  // |status| is used for the status of the whole FIDL request. We expect
  // |status| to be ZX_OK if the channel connects to a skip-block driver.
  // |op_status| refers to the status of the underlying read/write operation
  // and will be ZX_OK only if the read/write succeeds. It is NOT set if
  // the channel is not connected to a skip-block driver.
  zx_status_t op_status;

  zx_status_t status = fuchsia_hardware_skipblock_SkipBlockGetPartitionInfo(
      caller.borrow_channel(), &op_status, partition_info);

  return status == ZX_OK;
}

zx_status_t ZbiBootfsParser::FindBootZbi(uint32_t* read_offset, zbi_header_t* header) {
  zbi_header_t container_header;

  zx_status_t status = zbi_vmo_.read(&container_header, 0, sizeof(zbi_header_t));
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to read ZBI header from vmo.\n");
    return ZX_ERR_BAD_STATE;
  }

  printf("ZBI Container Header\n");
  printf("ZBI Type   = %08x\n", container_header.type);
  printf("ZBI Magic  = %08x\n", container_header.magic);
  printf("ZBI Extra  = %08x\n", container_header.extra);
  printf("ZBI Length = %08x (%u)\n", container_header.length, container_header.length);
  printf("ZBI Flags  = %08x\n", container_header.flags);

  if ((container_header.type != ZBI_TYPE_CONTAINER) ||
      (container_header.extra != ZBI_CONTAINER_MAGIC)) {
    printf("ZBI item does not have a container header\n");
    return ZX_ERR_BAD_STATE;
  }

  uint64_t bytes_to_read = container_header.length;
  uint64_t current_offset = sizeof(zbi_header_t);
  zbi_header_t item_header;

  while (bytes_to_read > 0) {
    status = zbi_vmo_.read(&item_header, current_offset, sizeof(zbi_header_t));
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to read ZBI header from vmo.\n");
      return status;
    }

    printf("ZBI Payload Header\n");
    printf("ZBI Type   = %08x\n", item_header.type);
    printf("ZBI Magic  = %08x\n", item_header.magic);
    printf("ZBI Extra  = %08x\n", item_header.extra);
    printf("ZBI Length = %08x (%u)\n", item_header.length, item_header.length);
    printf("ZBI Flags  = %08x\n", item_header.flags);

    uint64_t item_len = static_cast<uint64_t>(sizeof(zbi_header_t)) + item_header.length;

    // ZBI_ALIGN(uint32_t::max()) = 0 so the last ZBI_ALIGNMENT is excluded
    if (item_len > std::numeric_limits<uint32_t>::max() - ZBI_ALIGNMENT) {
      fprintf(stderr, "ZBI item exceeds uint32_t capacity\n");
      return ZX_ERR_INVALID_ARGS;
    }

    item_len = ZBI_ALIGN(item_len);

    if (item_len > bytes_to_read) {
      fprintf(stderr, "ZBI item too large (%lu > %lu)\n", item_len, bytes_to_read);
      return ZX_ERR_BAD_STATE;
    }

    switch (item_header.type) {
      case ZBI_TYPE_CONTAINER:
        fprintf(stderr, "Unexpected ZBI container header\n");
        status = ZX_ERR_INVALID_ARGS;
        break;

      case ZBI_TYPE_STORAGE_BOOTFS: {
        if (!(item_header.flags & ZBI_FLAG_STORAGE_COMPRESSED)) {
          fprintf(stderr, "Processing an uncompressed ZBI image is not currently supported\n");
          return ZX_ERR_NOT_SUPPORTED;
        }

        *read_offset = current_offset;
        memcpy(header, &item_header, sizeof(zbi_header_t));
        return ZX_OK;
      }

      default:
        printf("Unknown payload type, processing will stop\n");
        status = ZX_ERR_NOT_SUPPORTED;
        break;
    }

    current_offset += item_len;
    bytes_to_read -= item_len;
  }
  return status;
}

__EXPORT zx_status_t ZbiBootfsParser::ProcessZbi(const char* filename, Entry* entry) {
  uint32_t read_offset;
  zbi_header_t boot_header;
  zx_status_t status = FindBootZbi(&read_offset, &boot_header);
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo boot_vmo;
  uint32_t decompressed_size = boot_header.extra;

  if (decompressed_size > kMaxDecompressedZbiSize) {
    fprintf(stderr, "ZBI Decompressed size too large: %u > %u\n", decompressed_size,
            kMaxDecompressedZbiSize);
    return ZX_ERR_FILE_BIG;
  }

  status = zx::vmo::create(decompressed_size, 0, &boot_vmo);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to create boot vmo: %s\n", zx_status_get_string(status));
    return status;
  }

  status = Decompress(zbi_vmo_, read_offset + sizeof(zbi_header_t), boot_header.length, boot_vmo, 0,
                      decompressed_size);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to decompress bootfs: %s\n", zx_status_get_string(status));
    return status;
  }

  zbi_bootfs_dirent_t parsed_entry;
  status = FindEntry(zx::unowned_vmo(boot_vmo), filename, &parsed_entry);
  if (status != ZX_OK) {
    return status;
  }

  size_t data_len = parsed_entry.data_len;
  auto buffer = std::make_unique<uint8_t[]>(data_len);
  boot_vmo.read(buffer.get(), parsed_entry.data_off, data_len);

  zx::vmo vmo;
  zx::vmo::create(data_len, 0, &vmo);
  *entry = Entry{data_len, std::move(vmo)};

  entry->vmo.write(buffer.get(), 0, data_len);

  return ZX_OK;
}

__EXPORT zx_status_t ZbiBootfsParser::Init(const char* input) {
  zx_status_t status = LoadZbi(input);
  if (status != ZX_OK) {
    fprintf(stderr, "Error loading ZBI. Error code: %s\n", zx_status_get_string(status));
  }
  return status;
}

__EXPORT zx_status_t ZbiBootfsParser::LoadZbi(const char* input) {
  // Logic for skip-block devices.
  fuchsia_hardware_skipblock_PartitionInfo partition_info = {};

  zx::vmo vmo;
  fzl::VmoMapper mapping;

  size_t buf_size = 0;
  size_t input_bs = 0;

  fbl::unique_fd fd(open(input, O_RDONLY));
  if (!fd) {
    fprintf(stderr, "Couldn't open input file %s : %d\n", input, errno);
    return ZX_ERR_IO;
  }

  if ((IsSkipBlock(input, &partition_info))) {
    // Grab Block size for the partition we'd like to access
    input_bs = partition_info.block_size_bytes;

    // Set buffer size
    buf_size = partition_info.block_size_bytes;

    if (buf_size == 0) {
      fprintf(stderr, "Buffer size must be greater than zero\n");
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    zx_status_t status = zx::vmo::create(buf_size, ZX_VMO_RESIZABLE, &vmo);

    if (status != ZX_OK) {
      fprintf(stderr, "Error creating VMO\n");
      return status;
    }

    zx::vmo dup;
    status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
    if (status != ZX_OK) {
      fprintf(stderr, "Cannot duplicate handle\n");
      return status;
    }

    const uint32_t block_count = static_cast<uint32_t>(input_bs / partition_info.block_size_bytes);
    fuchsia_hardware_skipblock_ReadWriteOperation op = {
        .vmo = dup.release(),
        .vmo_offset = 0,
        .block = 0,
        .block_count = block_count,
    };

    fdio_cpp::FdioCaller caller(std::move(fd));

    fuchsia_hardware_skipblock_SkipBlockRead(caller.borrow_channel(), &op, &status);

    if (status != ZX_OK) {
      fprintf(stderr, "Failed to read skip-block partition. Error code: %d\n", status);
      return status;
    }

    // Check ZBI header for content length and set buffer size
    // accordingly
    zbi_header_t hdr;
    status = vmo.read(&hdr, 0, sizeof(zbi_header_t));
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to read ZBI header from vmo.\n");
      return status;
    }

    printf("ZBI container type = %08x\n", hdr.type);
    printf("ZBI payload length = %u\n", hdr.length);

    // Check if ZBI contents are larger than the size of one block
    // Resize the VMO accordingly
    if ((hdr.length + sizeof(zbi_header_t)) > buf_size) {
      vmo.set_size(buf_size + (hdr.length + sizeof(zbi_header_t)));

      uint64_t vmo_size;
      status = vmo.get_size(&vmo_size);
      if (status != ZX_OK || vmo_size == 0) {
        printf("Error resizing VMO\n");
        return status;
      }

      zx::vmo dup;
      status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
      if (status != ZX_OK) {
        fprintf(stderr, "Cannot duplicate handle\n");
        return status;
      }

      const uint32_t block_count =
          static_cast<uint32_t>(input_bs / partition_info.block_size_bytes);
      fuchsia_hardware_skipblock_ReadWriteOperation op = {
          .vmo = dup.release(),
          .vmo_offset = 0,
          .block = 0,
          .block_count = block_count,
      };

      zx_status_t status;
      fuchsia_hardware_skipblock_SkipBlockRead(caller.borrow_channel(), &op, &status);

      if (status != ZX_OK) {
        fprintf(stderr, "Failed to read skip-block partition. Error code: %d\n", status);
        return status;
      }
    }

  } else {
    // Check ZBI header for content length and set buffer size
    // accordingly
    char buf[sizeof(zbi_header_t)];

    if (read(fd.get(), buf, sizeof(zbi_header_t)) != sizeof(zbi_header_t)) {
      fprintf(stderr, "Failed to read header from zbi.\n");
      return ZX_ERR_IO;
    }

    zbi_header_t* hdr = reinterpret_cast<zbi_header_t*>(&buf);
    printf("ZBI container type = %08x\n", hdr->type);
    printf("ZBI payload length = %u\n", hdr->length);

    if (hdr->length == 0) {
      fprintf(stderr, "Payload length must be greater than zero\n");
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    buf_size = hdr->length + sizeof(zbi_header_t);

    zx_status_t status = mapping.CreateAndMap(buf_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                              &vmo, ZX_RIGHT_SAME_RIGHTS, 0);
    if (status != ZX_OK) {
      fprintf(stderr, "Error creating and mapping VMO\n");
      return status;
    }

    if (lseek(fd.get(), 0, SEEK_SET) != 0) {
      fprintf(stderr, "Failed to reset to beginning of fd\n");
      return ZX_ERR_IO;
    }

    // Read in input file (on disk) into buffer
    if (read(fd.get(), mapping.start(), mapping.size()) != static_cast<ssize_t>(mapping.size())) {
      fprintf(stderr, "Failed to read input file into buffer\n");
      return ZX_ERR_IO;
    }
  }

  zbi_vmo_ = std::move(vmo);
  return ZX_OK;
}

static zx_status_t DecompressZstd(zx::vmo& input, uint64_t input_offset, size_t input_size,
                                  zx::vmo& output, uint64_t output_offset, size_t output_size) {
  auto input_buffer = std::make_unique<std::byte[]>(input_size);
  zx_status_t status = input.read(input_buffer.get(), input_offset, input_size);
  if (status != ZX_OK) {
    return status;
  }

  auto output_buffer = std::make_unique<std::byte[]>(output_size);

  auto rc = ZSTD_decompress(output_buffer.get(), output_size, input_buffer.get(), input_size);
  if (ZSTD_isError(rc) || rc != output_size) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  status = output.write(output_buffer.get(), output_offset, output_size);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

static zx_status_t DecompressLz4f(zx::vmo& input, uint64_t input_offset, size_t input_size,
                                  zx::vmo& output, uint64_t output_offset, size_t output_size) {
  auto input_buffer = std::make_unique<std::byte[]>(input_size);
  zx_status_t status = input.read(input_buffer.get(), input_offset, input_size);
  if (status != ZX_OK) {
    return status;
  }

  auto output_buffer = std::make_unique<std::byte[]>(output_size);

  LZ4F_decompressionContext_t ctx;
  LZ4F_errorCode_t result = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
  if (LZ4F_isError(result)) {
    return ZX_ERR_INTERNAL;
  }

  // Calls freeDecompressionContext when cleanup goes out of scope
  auto cleanup = fbl::MakeAutoCall([&]() { LZ4F_freeDecompressionContext(ctx); });

  std::byte* dst = output_buffer.get();
  size_t dst_size = output_size;

  auto src = input_buffer.get();
  size_t src_size = input_size;
  do {
    if (dst_size == 0) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    size_t nwritten = dst_size, nread = src_size;
    static constexpr const LZ4F_decompressOptions_t kDecompressOpt{};
    result = LZ4F_decompress(ctx, dst, &nwritten, src, &nread, &kDecompressOpt);
    if (LZ4F_isError(result)) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    if (nread > src_size) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    src += nread;
    src_size -= nread;

    if (nwritten > dst_size) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    dst += nwritten;
    dst_size -= nwritten;
  } while (src_size > 0);

  if (dst_size > 0) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  status = output.write(output_buffer.get(), output_offset, output_size);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

static constexpr uint32_t kLz4fMagic = 0x184D2204;
static constexpr uint32_t kZstdMagic = 0xFD2FB528;

zx_status_t Decompress(zx::vmo& input, uint64_t input_offset, size_t input_size, zx::vmo& output,
                       uint64_t output_offset, size_t output_size) {
  uint32_t magic;

  zx_status_t status = input.read(&magic, input_offset, sizeof(magic));
  if (status != ZX_OK) {
    return status;
  }

  if (magic == kLz4fMagic) {
    return DecompressLz4f(input, input_offset, input_size, output, output_offset, output_size);
  }

  if (magic == kZstdMagic) {
    return DecompressZstd(input, input_offset, input_size, output, output_offset, output_size);
  }

  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace zbi_bootfs
