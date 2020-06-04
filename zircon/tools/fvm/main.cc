// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <string>

#include <fbl/alloc_checker.h>
#include <fvm-host/container.h>
#include <fvm-host/file-wrapper.h>
#include <fvm-host/format.h>
#include <fvm/sparse-reader.h>
#include <safemath/checked_math.h>

#include "fbl/auto_call.h"
#include "mtd.h"

#define DEFAULT_SLICE_SIZE (8lu * (1 << 20))
constexpr char kMinimumInodes[] = "--minimum-inodes";
constexpr char kMinimumData[] = "--minimum-data-bytes";
constexpr char kMaximumBytes[] = "--maximum-bytes";
constexpr char kEmptyMinfs[] = "--with-empty-minfs";

enum class DiskType {
  File = 0,
  Mtd = 1,
};

int usage(void) {
  fprintf(stderr, "usage: fvm [ output_path ] [ command ] [ <flags>* ] [ <input_paths>* ]\n");
  fprintf(stderr, "fvm performs host-side FVM and sparse file creation\n");
  fprintf(stderr, "Commands:\n");
  fprintf(stderr, " create : Creates an FVM partition\n");
  fprintf(stderr,
          " add : Adds a Minfs or Blobfs partition to an FVM (input path is"
          " required)\n");
  fprintf(stderr,
          " extend : Extends an FVM container to the specified size (length is"
          " required)\n");
  fprintf(stderr, " sparse : Creates a sparse file. One or more input paths are required.\n");
  fprintf(stderr, " pave : Creates an FVM container from a sparse file.\n");
  fprintf(stderr,
          " verify : Report basic information about sparse/fvm files and run fsck on"
          " contained partitions.\n");
  fprintf(stderr,
          " size : Prints the minimum size required in order to pave a sparse file."
          " If the --disk flag is provided, instead checks that the paved sparse file"
          " will fit within a disk of this size. On success, no information is"
          " outputted\n");
  fprintf(stderr,
          " used-data-size : Prints sum of the space, in bytes, used by data on \n"
          " different partitions. This does not include blocks used internally for \n"
          " superblock, bitmaps, inodes, or for journal,\n");
  fprintf(stderr, " used-inodes : Prints the sum of used inodes on different partitions.\n");
  fprintf(stderr,
          " used-size : Prints sum of the space, in bytes, used by data and by\n"
          " superblock, bitmaps, inodes, and journal different partitions. All of the\n"
          " reservations for non-data blocks are considered as used.\n");
  fprintf(stderr,
          " decompress : Decompresses a compressed sparse file. --sparse input path is"
          " required.\n");
  fprintf(stderr, "Flags (neither or both of offset/length must be specified):\n");
  fprintf(stderr,
          " --slice [bytes] - specify slice size - only valid on container creation.\n"
          "                   (default: %zu)\n",
          DEFAULT_SLICE_SIZE);
  fprintf(stderr,
          " --max-disk-size [bytes] Used for preallocating metadata. Only valid for sparse image. "
          "(defaults to 0)\n");
  fprintf(stderr, " --offset [bytes] - offset at which container begins (fvm only)\n");
  fprintf(stderr, " --length [bytes] - length of container within file (fvm only)\n");
  fprintf(stderr, " --compress - specify that file should be compressed (sparse only)\n");
  fprintf(stderr, " --disk [bytes] - Size of target disk (valid for size command only)\n");
  fprintf(stderr, " --disk-type [file OR mtd] - Type of target disk (pave only)\n");
  fprintf(stderr, " --max-bad-blocks [number] - Max bad blocks for FTL (pave on mtd only)\n");
  fprintf(stderr, "Input options:\n");
  fprintf(stderr, " --blob [path] [reserve options] - Add path as blob type (must be blobfs)\n");
  fprintf(stderr,
          " --data [path] [reserve options] - Add path as encrypted data type (must"
          " be minfs)\n");
  fprintf(stderr, " --data-unsafe [path] - Add path as unencrypted data type (must be minfs)\n");
  fprintf(stderr, " --system [path] - Add path as system type (must be minfs)\n");
  fprintf(stderr, " --default [path] - Add generic path\n");
  fprintf(stderr, " --sparse [path] - Path to compressed sparse file\n");
  fprintf(stderr,
          " --resize-image-file-to-fit - When used with create/extend command, the output image "
          "file will "
          "be resized to just fit the metadata header and added partitions. Disk size specified in "
          "the header remains the same. It's useful for reducing the size of the image file for "
          "flashing\n");
  fprintf(
      stderr,
      " --length-is-lowerbound - When used with extend command, if current disk size is already "
      "no smaller than the specified size, the command will be no-op. If the option is not "
      "specified, it will error out in this case.\n");
  fprintf(stderr, "reserve options:\n");
  fprintf(stderr,
          " These options, on success, reserve additional fvm slices for data/inodes.\n"
          " The number of bytes reserved may exceed the actual bytes needed due to\n"
          " rounding up to slice boundary.\n");
  fprintf(stderr,
          " --minimum-inodes inode_count - number of inodes to reserve\n"
          "                                Blobfs inode size is %u\n"
          "                                Minfs inode size is %u\n",
          blobfs::kBlobfsInodeSize, minfs::kMinfsInodeSize);

  fprintf(stderr,
          " --minimum-data-bytes data_bytes - number of bytes to reserve for data\n"
          "                                   in the fs\n"
          "                                   Blobfs block size is %u\n"
          "                                   Minfs block size is %u\n",
          blobfs::kBlobfsBlockSize, minfs::kMinfsBlockSize);
  fprintf(stderr,
          " --maximum-bytes bytes - Places an upper bound of <bytes> on the total\n"
          "                         number of bytes which may be used by the partition.\n"
          "                         Returns an error if more space is necessary to\n"
          "                         create the requested filesystem.\n");
  fprintf(stderr,
          " --with-empty-minfs    - Adds a placeholder partition that will be formatted on boot,\n"
          "                         to minfs. The partition will be the 'data' partition.\n");
  exit(-1);
}

int parse_size(const char* size_str, size_t* out) {
  char* end;
  size_t size = strtoull(size_str, &end, 10);

  switch (end[0]) {
    case 'K':
    case 'k':
      size *= 1024;
      end++;
      break;
    case 'M':
    case 'm':
      size *= (1024 * 1024);
      end++;
      break;
    case 'G':
    case 'g':
      size *= (1024 * 1024 * 1024);
      end++;
      break;
  }

  if (end[0] || size == 0) {
    fprintf(stderr, "Bad size: %s\n", size_str);
    return -1;
  }

  *out = size;
  return 0;
}

int add_partitions(Container* container, int argc, char** argv) {
  auto add_corrupted_partition =
      fbl::MakeAutoCall([&]() { container->AddCorruptedPartition(kDataTypeName, 0); });
  bool seen = false;
  for (int i = 0; i < argc;) {
    if (argc - i < 2 || argv[i][0] != '-' || argv[i][1] != '-') {
      usage();
    }

    const char* partition_type = argv[i] + 2;
    const char* partition_path = argv[i + 1];
    if (i < argc && strcmp(argv[i], kEmptyMinfs) == 0) {
      seen = true;
      i++;
      continue;
    }

    std::optional<uint64_t> inodes = {}, data = {}, total_bytes = {};
    i += 2;

    while (true) {
      size_t size;
      if ((i + 2) <= argc && strcmp(argv[i], kMinimumInodes) == 0) {
        if (parse_size(argv[i + 1], &size) < 0) {
          usage();
          return -1;
        }
        inodes = safemath::checked_cast<uint64_t>(size);
        i += 2;
      } else if ((i + 2) <= argc && strcmp(argv[i], kMinimumData) == 0) {
        if (parse_size(argv[i + 1], &size) < 0) {
          usage();
          return -1;
        }
        data = safemath::checked_cast<uint64_t>(size);
        i += 2;
      } else if ((i + 2) <= argc && strcmp(argv[i], kMaximumBytes) == 0) {
        if (parse_size(argv[i + 1], &size) < 0) {
          usage();
          return -1;
        }
        total_bytes = safemath::checked_cast<uint64_t>(size);
        i += 2;
      } else {
        break;
      }
    }

    FvmReservation reserve(inodes, data, total_bytes);
    zx_status_t status = container->AddPartition(partition_path, partition_type, &reserve);
    if (status == ZX_ERR_BUFFER_TOO_SMALL) {
      fprintf(stderr, "Failed to add partition\n");
      reserve.Dump(stderr);
      return -1;
    }
  }
  if (!seen) {
    add_corrupted_partition.cancel();
  }

  return 0;
}

size_t get_disk_size(const char* path, size_t offset) {
  fbl::unique_fd fd(open(path, O_RDONLY, 0644));

  if (fd) {
    struct stat s;
    if (fstat(fd.get(), &s) < 0) {
      fprintf(stderr, "Failed to stat %s\n", path);
      exit(-1);
    }

    return s.st_size - offset;
  }

  return 0;
}

zx_status_t ParseDiskType(const char* type_str, DiskType* out) {
  if (!strcmp(type_str, "file")) {
    *out = DiskType::File;
    return ZX_OK;
  } else if (!strcmp(type_str, "mtd")) {
    *out = DiskType::Mtd;
    return ZX_OK;
  }

  fprintf(stderr, "Unknown disk type: '%s'. Expected 'file' or 'mtd'.\n", type_str);
  return ZX_ERR_INVALID_ARGS;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    usage();
  }

  int i = 1;
  const char* path = argv[i++];     // Output path
  const char* command = argv[i++];  // Command

  size_t length = 0;
  size_t offset = 0;
  size_t slice_size = DEFAULT_SLICE_SIZE;
  size_t disk_size = 0;

  size_t max_bad_blocks = 0;
  size_t max_disk_size = 0;
  bool is_max_bad_blocks_set = false;
  DiskType disk_type = DiskType::File;

  bool should_unlink = true;
  bool resize_image_file_to_fit = false;
  bool length_is_lower_bound = false;
  uint32_t flags = 0;

  while (i < argc) {
    if (!strcmp(argv[i], "--slice") && i + 1 < argc) {
      if (parse_size(argv[++i], &slice_size) < 0) {
        return -1;
      }
      if (!slice_size || slice_size % blobfs::kBlobfsBlockSize ||
          slice_size % minfs::kMinfsBlockSize) {
        fprintf(stderr, "Invalid slice size - must be a multiple of %u and %u\n",
                blobfs::kBlobfsBlockSize, minfs::kMinfsBlockSize);
        return -1;
      }
    } else if (!strcmp(argv[i], "--offset") && i + 1 < argc) {
      should_unlink = false;
      if (parse_size(argv[++i], &offset) < 0) {
        return -1;
      }
    } else if (!strcmp(argv[i], "--length") && i + 1 < argc) {
      if (parse_size(argv[++i], &length) < 0) {
        return -1;
      }
    } else if (!strcmp(argv[i], "--compress")) {
      if (!strcmp(argv[++i], "lz4")) {
        flags |= fvm::kSparseFlagLz4;
      } else {
        fprintf(stderr, "Invalid compression type\n");
        return -1;
      }
    } else if (!strcmp(argv[i], "--disk-type")) {
      if (ParseDiskType(argv[++i], &disk_type) != ZX_OK) {
        return -1;
      }
    } else if (!strcmp(argv[i], "--max-bad-blocks")) {
      max_bad_blocks = std::stoul(argv[++i], nullptr, 10);
      is_max_bad_blocks_set = true;
    } else if (!strcmp(argv[i], "--disk")) {
      if (parse_size(argv[++i], &disk_size) < 0) {
        return -1;
      }
    } else if (!strcmp(argv[i], "--max-disk-size") && i + 1 < argc) {
      if (parse_size(argv[++i], &max_disk_size) < 0) {
        return -1;
      }
    } else if (!strcmp(argv[i], "--resize-image-file-to-fit")) {
      resize_image_file_to_fit = true;
    } else if (!strcmp(argv[i], "--length-is-lowerbound")) {
      length_is_lower_bound = true;
    } else {
      break;
    }

    ++i;
  }

  if (!strcmp(command, "create") && should_unlink) {
    unlink(path);
  }

  // If length was not specified, use remainder of file after offset.
  // get_disk_size may return 0 due to MTD behavior with fstat.
  // This scenario is checked in the pave section below.
  if (length == 0 && disk_type != DiskType::Mtd) {
    length = get_disk_size(path, offset);
  }

  if (disk_type == DiskType::Mtd) {
    if (strcmp(command, "pave")) {
      fprintf(stderr, "Only the pave command is supported for MTD.\n");
      return -1;
    }

    if (!is_max_bad_blocks_set) {
      fprintf(stderr, "--max-bad-blocks is required when paving to MTD.\n");
      return -1;
    }
  }

  if (!strcmp(command, "create")) {
    // If length was specified, an offset was not, we were asked to create a
    // file, and the file does not exist, truncate it to the given length.
    if (length != 0 && offset == 0) {
      fbl::unique_fd fd(open(path, O_CREAT | O_EXCL | O_WRONLY, 0644));

      if (fd) {
        ftruncate(fd.get(), length);
      }
    }

    std::unique_ptr<FvmContainer> fvmContainer;
    if (FvmContainer::CreateNew(path, slice_size, offset, length, &fvmContainer) != ZX_OK) {
      return -1;
    }

    if (add_partitions(fvmContainer.get(), argc - i, argv + i) < 0) {
      return -1;
    }

    if (resize_image_file_to_fit) {
      fvmContainer->SetResizeImageFileToFit(FvmContainer::ResizeImageFileToFitOption::YES);
    }

    if (fvmContainer->Commit() != ZX_OK) {
      return -1;
    }
  } else if (!strcmp(command, "add")) {
    std::unique_ptr<FvmContainer> fvmContainer;
    if (FvmContainer::CreateExisting(path, offset, &fvmContainer) != ZX_OK) {
      return -1;
    }

    if (add_partitions(fvmContainer.get(), argc - i, argv + i) < 0) {
      return -1;
    }

    if (fvmContainer->Commit() != ZX_OK) {
      return -1;
    }
  } else if (!strcmp(command, "extend")) {
    if (length == 0 || offset > 0) {
      usage();
    }

    size_t disk_size = get_disk_size(path, 0);

    if (length <= disk_size) {
      fprintf(stderr, "Cannot extend to a value %zu less than current size %zu\n", length,
              disk_size);
      usage();
    }

    std::unique_ptr<FvmContainer> fvmContainer;
    if (FvmContainer::CreateExisting(path, offset, &fvmContainer) != ZX_OK) {
      return -1;
    }

    if (length_is_lower_bound) {
      fvmContainer->SetExtendLengthType(FvmContainer::ExtendLengthType::LOWER_BOUND);
    }

    if (fvmContainer->Extend(length) != ZX_OK) {
      return -1;
    }
  } else if (!strcmp(command, "sparse")) {
    if (offset) {
      fprintf(stderr, "Invalid sparse flags\n");
      return -1;
    }

    std::unique_ptr<SparseContainer> sparseContainer;
    if (SparseContainer::CreateNew(path, slice_size, flags, max_disk_size, &sparseContainer) !=
        ZX_OK) {
      return -1;
    }

    if (add_partitions(sparseContainer.get(), argc - i, argv + i) < 0) {
      return -1;
    }

    if (sparseContainer->Commit() != ZX_OK) {
      return -1;
    }
  } else if (!strcmp(command, "verify")) {
    std::unique_ptr<Container> containerData;
    if (Container::Create(path, offset, flags, &containerData) != ZX_OK) {
      return -1;
    }

    if (containerData->Verify() != ZX_OK) {
      return -1;
    }
  } else if (!strcmp(command, "decompress")) {
    if (argc - i != 2) {
      usage();
      return -1;
    }

    char* input_type = argv[i];
    char* input_path = argv[i + 1];

    if (strcmp(input_type, "--sparse")) {
      usage();
      return -1;
    }

    std::unique_ptr<SparseContainer> compressedContainer;
    if (SparseContainer::CreateExisting(input_path, &compressedContainer) != ZX_OK) {
      return -1;
    }

    if (compressedContainer->Decompress(path) != ZX_OK) {
      return -1;
    }

    std::unique_ptr<SparseContainer> sparseContainer;
    if (SparseContainer::CreateExisting(path, &sparseContainer) != ZX_OK) {
      return -1;
    }

    if (sparseContainer->Verify() != ZX_OK) {
      return -1;
    }
  } else if (!strcmp(command, "size")) {
    std::unique_ptr<SparseContainer> sparseContainer;
    if (SparseContainer::CreateExisting(path, &sparseContainer) != ZX_OK) {
      return -1;
    }

    if (disk_size == 0) {
      printf("%" PRIu64 "\n", sparseContainer->CalculateDiskSize());
    } else if (sparseContainer->CheckDiskSize(disk_size) != ZX_OK) {
      fprintf(stderr, "Sparse container will not fit in target disk size\n");
      return -1;
    }
  } else if (!strcmp(command, "used-data-size")) {
    std::unique_ptr<SparseContainer> sparseContainer;
    if (SparseContainer::CreateExisting(path, &sparseContainer) != ZX_OK) {
      return -1;
    }

    uint64_t size;

    if (sparseContainer->UsedDataSize(&size) != ZX_OK) {
      return -1;
    }
    printf("%" PRIu64 "\n", size);
  } else if (!strcmp(command, "used-inodes")) {
    std::unique_ptr<SparseContainer> sparseContainer;
    if (SparseContainer::CreateExisting(path, &sparseContainer) != ZX_OK) {
      return -1;
    }

    uint64_t used_inodes;

    if (sparseContainer->UsedInodes(&used_inodes) != ZX_OK) {
      return -1;
    }
    printf("%" PRIu64 "\n", used_inodes);
  } else if (!strcmp(command, "used-size")) {
    std::unique_ptr<SparseContainer> sparseContainer;
    if (SparseContainer::CreateExisting(path, &sparseContainer) != ZX_OK) {
      return -1;
    }

    uint64_t size;

    if (sparseContainer->UsedSize(&size) != ZX_OK) {
      return -1;
    }
    printf("%" PRIu64 "\n", size);
  } else if (!strcmp(command, "pave")) {
    char* input_type = argv[i];
    char* input_path = argv[i + 1];

    if (strcmp(input_type, "--sparse")) {
      fprintf(stderr, "pave command only accepts --sparse input option\n");
      usage();
      return -1;
    }

    std::unique_ptr<SparseContainer> sparseData;
    if (SparseContainer::CreateExisting(input_path, &sparseData) != ZX_OK) {
      return -1;
    }

    std::unique_ptr<fvm::host::FileWrapper> wrapper;

    if (disk_type == DiskType::File) {
      std::unique_ptr<fvm::host::UniqueFdWrapper> unique_fd_wrapper;
      if (fvm::host::UniqueFdWrapper::Open(path, O_CREAT | O_WRONLY, 0644, &unique_fd_wrapper) !=
          ZX_OK) {
        return -1;
      }

      wrapper = std::move(unique_fd_wrapper);
    } else if (disk_type == DiskType::Mtd) {
      zx_status_t status =
          CreateFileWrapperFromMtd(path, safemath::saturated_cast<uint32_t>(offset),
                                   safemath::saturated_cast<uint32_t>(max_bad_blocks), &wrapper);

      if (status != ZX_OK) {
        return -1;
      }

      // The byte offset into the output file is handled by CreateFileWrapperFromMtd.
      offset = 0;

      // Length may be 0 at this point if the user did not specify a size.
      // Use all of the space reported by the FTL in this case.
      if (length == 0) {
        length = wrapper->Size();
      }
    } else {
      fprintf(stderr, "Unknown disk type\n");
      return -1;
    }

    if (sparseData->Pave(std::move(wrapper), offset, length) != ZX_OK) {
      return -1;
    }
  } else {
    usage();
  }

  return 0;
}
