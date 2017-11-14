// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include "fvm/container.h"

int usage(void) {
    fprintf(stderr, "usage: fvm [ output_path ] [ command ] [ <flags>* ] [ <input_paths>* ]\n");
    fprintf(stderr, "fvm performs host-side FVM and sparse file creation\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, " create : Creates an FVM partition\n");
    fprintf(stderr, " verify : Report basic information about sparse/fvm files and run fsck on"                     "contained partitions\n");
    fprintf(stderr, " add : Adds a Minfs or Blobstore partition to an FVM (input path is"
                    " required)\n");
    fprintf(stderr, " sparse : Creates a sparse file. One or more input paths are required.\n");
    fprintf(stderr, "Flags (neither or both must be specified):\n");
    fprintf(stderr, " --offset [bytes] - offset at which container begins (fvm only)\n");
    fprintf(stderr, " --length [bytes] - length of container within file (fvm only)\n");
    fprintf(stderr, "Input options:\n");
    fprintf(stderr, " --blobstore [path] - Add path as blobstore type (must be blobstore)\n");
    fprintf(stderr, " --data [path] - Add path as data type (must be minfs)\n");
    fprintf(stderr, " --system [path] - Add path as system type (must be minfs)\n");
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

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
    }

    unsigned i = 1;
    char* path = argv[i++]; // Output path
    char* command = argv[i++]; // Command

    size_t length = 0;
    size_t offset = 0;

    while (i < argc) {
        if (!strcmp(argv[i], "--offset") && i + 1 < argc) {
            offset = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--length") && i + 1 < argc) {
            length = atoll(argv[++i]);
        } else {
            break;
        }

        ++i;
    }

    // If length was not specified, use length of file.
    if (i == 3) {
        fbl::unique_fd fd(open(path, O_PATH, 0644));

        if (!fd) {
            fprintf(stderr, "Unable to open path\n");
            return -1;
        }

        struct stat s;
        if (fstat(fd.get(), &s) < 0) {
            fprintf(stderr, "Failed to stat %s\n", path);
            return -1;
        }

        length = s.st_size;
    } else if (i != 7) {
        fprintf(stderr, "Invalid flags\n");
        return -1;
    }

    //TODO(planders): take this as argument?
    size_t slice_size = 64lu * (1 << 20);

    if (!strcmp(command, "create")) {
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
    } else if (!strcmp(command, "verify")) {
        fbl::unique_ptr<Container> containerData;
        if (Container::Create(path, offset, length, &containerData) != ZX_OK) {
            return -1;
        }

        if (containerData->Verify() != ZX_OK) {
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
    } else if (!strcmp(command, "sparse")) {
        if (i == 7) {
            fprintf(stderr, "Invalid sparse flags\n");
            return -1;
        }

        fbl::unique_ptr<SparseContainer> sparseContainer;
        if (SparseContainer::Create(path, slice_size, &sparseContainer) != ZX_OK) {
            return -1;
        }

        if (add_partitions(sparseContainer.get(), argc - i, argv + i) < 0) {
            return -1;
        }

        if (sparseContainer->Commit() != ZX_OK) {
            return -1;
        }
    } else {
        usage();
    }

    return 0;
}