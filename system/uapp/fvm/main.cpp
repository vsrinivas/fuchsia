// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include "container/container.h"

int usage(void) {
    fprintf(stderr, "usage: fvm [ output_path ] [ command ] [ <input_paths>* ]\n");
    fprintf(stderr, "fvm performs host-side FVM and sparse file creation\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, " create : Creates an FVM partition\n");
    fprintf(stderr, " verify : Report basic information about sparse/fvm files and run fsck on"                     "contained partitions\n");
    fprintf(stderr, " add : Adds a Minfs or Blobstore partition to an FVM (input path is"
                    " required)\n");
    fprintf(stderr, " sparse : Creates a sparse file. One or more input paths are required.\n");
    fprintf(stderr, "Input options:\n");
    fprintf(stderr, " --blobstore [path] - Add path as blobstore type (must be blobstore)\n");
    fprintf(stderr, " --data [path] - Add path as data type (must be minfs)\n");
    fprintf(stderr, " --system [path] - Add path as system type (must be minfs)\n");
    exit(-1);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
    }

    char* path = argv[1]; // Output path
    char* command = argv[2]; // Command

    //TODO(planders): take this as argument?
    size_t slice_size = 64lu * (1 << 20);

    if (!strcmp(command, "create")) {
        FvmContainer fvmContainer(path, slice_size);
        if (fvmContainer.Init() != ZX_OK) {
            return -1;
        }

        if (fvmContainer.Commit() != ZX_OK) {
            return -1;
        }
    } else if (!strcmp(command, "verify")) {
        fbl::unique_ptr<Container> containerData;
        if (Container::Create(path, &containerData) != ZX_OK) {
            return -1;
        }

        if (containerData->Verify() != ZX_OK) {
            return -1;
        }
    } else if (!strcmp(command, "add")) {
        fbl::unique_ptr<FvmContainer> fvmContainer;
        if (FvmContainer::Create(path, slice_size, &fvmContainer) != ZX_OK) {
            return -1;
        }

        for (unsigned i = 3; i < argc; i += 2) {
            if (argc - i < 2 || argv[i][0] != '-' || argv[i][1] != '-') {
                usage();
            }

            char* partition_type = argv[i] + 2;
            char* partition_path = argv[i + 1];
            if ((fvmContainer->AddPartition(partition_path, partition_type)) != ZX_OK) {
                printf("Failed to add partition\n");
                return -1;
            }
        }

        if (fvmContainer->Commit() != ZX_OK) {
            return -1;
        }
    } else if (!strcmp(command, "sparse")) {
        fbl::unique_ptr<SparseContainer> sparseContainer;
        if (SparseContainer::Create(path, slice_size, &sparseContainer) != ZX_OK) {
            return -1;
        }

        for (unsigned i = 3; i < argc; i += 2) {
            if (argc - i < 2 || argv[i][0] != '-' || argv[i][1] != '-') {
                usage();
            }

            char* partition_type = argv[i] + 2;
            char* partition_path = argv[i + 1];
            if ((sparseContainer->AddPartition(partition_path, partition_type)) != ZX_OK) {
                printf("Failed to create sparse partition\n");
                return -1;
            }
        }

        if (sparseContainer->Commit() != ZX_OK) {
            return -1;
        }
    } else {
        usage();
    }

    return 0;
}