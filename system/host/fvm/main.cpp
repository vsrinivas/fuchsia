// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <unistd.h>

#include "fvm/container.h"

#define DEFAULT_SLICE_SIZE (64lu * (1 << 20))

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
    fprintf(stderr, " verify : Report basic information about sparse/fvm files and run fsck on"
                        " contained partitions\n");
    fprintf(stderr, "Flags (neither or both of offset/length must be specified):\n");
    fprintf(stderr, " --slice [bytes] - specify slice size (default: %zu)\n", DEFAULT_SLICE_SIZE);
    fprintf(stderr, " --offset [bytes] - offset at which container begins (fvm only)\n");
    fprintf(stderr, " --length [bytes] - length of container within file (fvm only)\n");
    fprintf(stderr, " --compress - specify that file should be compressed (sparse only)\n");
    fprintf(stderr, "Input options:\n");
    fprintf(stderr, " --blob [path] - Add path as blob type (must be blobfs)\n");
    fprintf(stderr, " --data [path] - Add path as data type (must be minfs)\n");
    fprintf(stderr, " --system [path] - Add path as system type (must be minfs)\n");
    fprintf(stderr, " --default [path] - Add generic path\n");
    exit(-1);
}

int add_partitions(Container* container, int argc, char** argv) {
    for (unsigned i = 0; i < argc; i += 2) {
        if (argc - i < 2 || argv[i][0] != '-' || argv[i][1] != '-') {
            usage();
        }

        char* partition_type = argv[i] + 2;
        char* partition_path = argv[i + 1];
        if ((container->AddPartition(partition_path, partition_type)) != ZX_OK) {
            fprintf(stderr, "Failed to add partition\n");
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

    unsigned i = 1;
    const char* path = argv[i++]; // Output path
    const char* command = argv[i++]; // Command

    size_t length = 0;
    size_t offset = 0;
    size_t slice_size = DEFAULT_SLICE_SIZE;
    bool should_unlink = true;
    compress_type_t compress = NONE;

    while (i < argc) {
        if (!strcmp(argv[i], "--slice") && i + 1 < argc) {
            slice_size = atoll(argv[++i]);
            if (!slice_size ||
                slice_size % blobfs::kBlobfsBlockSize ||
                slice_size % minfs::kMinfsBlockSize) {
                fprintf(stderr, "Invalid slice size - must be a multiple of %u and %u\n",
                        blobfs::kBlobfsBlockSize, minfs::kMinfsBlockSize);
                return -1;
            }
        } else if (!strcmp(argv[i], "--offset") && i + 1 < argc) {
            should_unlink = false;
            offset = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--length") && i + 1 < argc) {
            length = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--compress")) {
            if (!strcmp(argv[++i], "lz4")) {
                compress = LZ4;
            } else {
                fprintf(stderr, "Invalid compression type\n");
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
            fbl::unique_fd fd(open(path, O_CREAT|O_EXCL|O_WRONLY, 0644));

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
        fbl::AllocChecker ac;
        fbl::unique_ptr<FvmContainer> fvmContainer(new (&ac) FvmContainer(path, slice_size, offset,
                                                                          length));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
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

        fbl::AllocChecker ac;
        fbl::unique_ptr<FvmContainer> fvmContainer(new (&ac) FvmContainer(path, slice_size, offset,
                                                                          disk_size));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        if (fvmContainer->Extend(length) != ZX_OK) {
            return -1;
        }
    } else if (!strcmp(command, "sparse")) {
        if (offset) {
            fprintf(stderr, "Invalid sparse flags\n");
            return -1;
        }

        fbl::unique_ptr<SparseContainer> sparseContainer;
        if (SparseContainer::Create(path, slice_size, compress, &sparseContainer) != ZX_OK) {
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
        if (Container::Create(path, offset, length, &containerData) != ZX_OK) {
            return -1;
        }

        if (containerData->Verify() != ZX_OK) {
            return -1;
        }
    } else {
        usage();
    }

    return 0;
}
