// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <fvm-host/container.h>
#include <fvm-host/file-wrapper.h>
#include <fvm/sparse-reader.h>
#include <safemath/checked_math.h>

#define DEFAULT_SLICE_SIZE (8lu * (1 << 20))
constexpr char kMinimumInodes[] = "--minimum-inodes";
constexpr char kMinimumData[] = "--minimum-data-bytes";
constexpr char kMaximumBytes[] = "--maximum-bytes";

int usage(void) {
    fprintf(stderr, "usage: fvm [ output_path ] [ command ] [ <flags>* ] [ <input_paths>* ]\n");
    fprintf(stderr, "fvm performs host-side FVM and sparse file creation\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, " create : Creates an FVM partition\n");
    fprintf(stderr, " add : Adds a Minfs or Blobfs partition to an FVM (input path is"
                    " required)\n");
    fprintf(stderr, " extend : Extends an FVM container to the specified size (length is"
                    " required)\n");
    fprintf(stderr, " sparse : Creates a sparse file. One or more input paths are required.\n");
    fprintf(stderr, " pave : Creates an FVM container from a sparse file.\n");
    fprintf(stderr, " verify : Report basic information about sparse/fvm files and run fsck on"
                    " contained partitions.\n");
    fprintf(stderr, " size : Prints the minimum size required in order to pave a sparse file."
                    " If the --disk flag is provided, instead checks that the paved sparse file"
                    " will fit within a disk of this size. On success, no information is"
                    " outputted\n");
    fprintf(stderr, " decompress : Decompresses a compressed sparse file. --sparse input path is"
                    " required.\n");
    fprintf(stderr, "Flags (neither or both of offset/length must be specified):\n");
    fprintf(stderr, " --slice [bytes] - specify slice size (default: %zu)\n", DEFAULT_SLICE_SIZE);
    fprintf(stderr, " --offset [bytes] - offset at which container begins (fvm only)\n");
    fprintf(stderr, " --length [bytes] - length of container within file (fvm only)\n");
    fprintf(stderr, " --compress - specify that file should be compressed (sparse only)\n");
    fprintf(stderr, " --disk [bytes] - Size of target disk (valid for size command only)\n");
    fprintf(stderr, "Input options:\n");
    fprintf(stderr, " --blob [path] [reserve options] - Add path as blob type (must be blobfs)\n");
    fprintf(stderr, " --data [path] [reserve options] - Add path as encrypted data type (must"
                    " be minfs)\n");
    fprintf(stderr, " --data-unsafe [path] - Add path as unencrypted data type (must be minfs)\n");
    fprintf(stderr, " --system [path] - Add path as system type (must be minfs)\n");
    fprintf(stderr, " --default [path] - Add generic path\n");
    fprintf(stderr, " --sparse [path] - Path to compressed sparse file\n");
    fprintf(stderr, "reserve options:\n");
    fprintf(stderr, " These options, on success, reserve additional fvm slices for data/inodes.\n"
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
    fprintf(stderr, " --maximum-bytes bytes - Places an upper bound of <bytes> on the total\n"
                    "                         number of bytes which may be used by the partition.\n"
                    "                         Returns an error if more space is necessary to\n"
                    "                         create the requested filesystem.\n");
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
    for (int i = 0; i < argc;) {
        if (argc - i < 2 || argv[i][0] != '-' || argv[i][1] != '-') {
            usage();
        }

        const char* partition_type = argv[i] + 2;
        const char* partition_path = argv[i + 1];
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

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
    }

    int i = 1;
    const char* path = argv[i++];    // Output path
    const char* command = argv[i++]; // Command

    size_t length = 0;
    size_t offset = 0;
    size_t slice_size = DEFAULT_SLICE_SIZE;
    size_t disk_size = 0;
    bool should_unlink = true;
    uint32_t flags = 0;
    while (i < argc) {
        if (!strcmp(argv[i], "--slice") && i + 1 < argc) {
            if (parse_size(argv[++i], &slice_size) < 0) {
                return -1;
            }
            if (!slice_size ||
                slice_size % blobfs::kBlobfsBlockSize ||
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
        } else if (!strcmp(argv[i], "--disk")) {
            if (parse_size(argv[++i], &disk_size) < 0) {
                return -1;
            }
        } else {
            break;
        }

        ++i;
    }

    if (!strcmp(command, "create") && should_unlink) {
        unlink(path);
    }

    // If length was not specified, use remainder of file after offset
    if (length == 0) {
        length = get_disk_size(path, offset);
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

        fbl::unique_ptr<FvmContainer> fvmContainer;
        if (FvmContainer::Create(path, slice_size, offset, length, &fvmContainer) != ZX_OK) {
            return -1;
        }

        if (add_partitions(fvmContainer.get(), argc - i, argv + i) < 0) {
            return -1;
        }

        if (fvmContainer->Commit() != ZX_OK) {
            return -1;
        }
    } else if (!strcmp(command, "add")) {
        fbl::unique_ptr<FvmContainer> fvmContainer(new FvmContainer(path, slice_size, offset,
                                                                    length));

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

        fbl::unique_ptr<FvmContainer> fvmContainer(new FvmContainer(path, slice_size, offset,
                                                                    disk_size));

        if (fvmContainer->Extend(length) != ZX_OK) {
            return -1;
        }
    } else if (!strcmp(command, "sparse")) {
        if (offset) {
            fprintf(stderr, "Invalid sparse flags\n");
            return -1;
        }

        fbl::unique_ptr<SparseContainer> sparseContainer;
        if (SparseContainer::Create(path, slice_size, flags, &sparseContainer) != ZX_OK) {
            return -1;
        }

        if (add_partitions(sparseContainer.get(), argc - i, argv + i) < 0) {
            return -1;
        }

        if (sparseContainer->Commit() != ZX_OK) {
            return -1;
        }
    } else if (!strcmp(command, "verify")) {
        fbl::unique_ptr<Container> containerData;
        if (Container::Create(path, offset, length, flags, &containerData) != ZX_OK) {
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

        SparseContainer compressedContainer(input_path, slice_size, flags);
        if (compressedContainer.Decompress(path) != ZX_OK) {
            return -1;
        }

        SparseContainer sparseContainer(path, slice_size, flags);
        if (sparseContainer.Verify() != ZX_OK) {
            return -1;
        }
    } else if (!strcmp(command, "size")) {
        SparseContainer sparseContainer(path, slice_size, flags);

        if (disk_size == 0) {
            printf("%" PRIu64 "\n", sparseContainer.CalculateDiskSize());
        } else if (sparseContainer.CheckDiskSize(disk_size) != ZX_OK) {
            fprintf(stderr, "Sparse container will not fit in target disk size\n");
            return -1;
        }
    } else if (!strcmp(command, "pave")) {
        char* input_type = argv[i];
        char* input_path = argv[i + 1];

        if (strcmp(input_type, "--sparse")) {
            fprintf(stderr, "pave command only accepts --sparse input option\n");
            usage();
            return -1;
        }

        SparseContainer sparseData(input_path, slice_size, flags);
        fbl::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;

        if (fvm::host::UniqueFdWrapper::Open(path, O_CREAT | O_WRONLY, 0644, &wrapper) != ZX_OK) {
            return -1;
        }

        if (sparseData.Pave(std::move(wrapper), offset, length) != ZX_OK) {
            return -1;
        }
    } else {
        usage();
    }

    return 0;
}
