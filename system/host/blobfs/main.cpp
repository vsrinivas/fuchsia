// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>
#include <vector>

#include <blobfs/fsck.h>
#include <fbl/auto_call.h>
#include <sys/stat.h>

#include "blobfs.h"

// Add the blob located at |path| on host to the |blobfs| blobfs store.
zx_status_t AddBlob(blobfs::Blobfs* blobfs, const char* path) {
    fbl::unique_fd data_fd(open(path, O_RDONLY, 0644));
    if (!data_fd) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return ZX_ERR_IO;
    }
    zx_status_t status;
    if ((status = blobfs::blobfs_add_blob(blobfs, data_fd.get())) != ZX_OK) {
        if (status != ZX_ERR_ALREADY_EXISTS) {
            fprintf(stderr, "blobfs: Failed to add blob '%s': %d\n", path, status);
            return status;
        }
    }
    return ZX_OK;
}

zx_status_t BlobfsCreator::Usage() {
    zx_status_t status = FsCreator::Usage();

    // Additional information about manifest format.
    fprintf(stderr, "\nEach manifest line must adhere to one of the following formats:\n");
    fprintf(stderr, "\t'dst/path=src/path'\n");
    fprintf(stderr, "\t'dst/path'\n");
    fprintf(stderr, "with one dst/src pair or single dst per line.\n");
    return status;
}

bool BlobfsCreator::IsCommandValid(Command command) {
    switch (command) {
    case Command::kMkfs:
    case Command::kFsck:
    case Command::kAdd:
        return true;
    default:
        return false;
    }
}

bool BlobfsCreator::IsOptionValid(Option option) {
    //TODO(planders): Add offset and length support to blobfs.
    switch (option) {
    case Option::kDepfile:
    case Option::kReadonly:
    case Option::kHelp:
        return true;
    default:
        return false;
    }
}

bool BlobfsCreator::IsArgumentValid(Argument argument) {
    switch (argument) {
    case Argument::kManifest:
    case Argument::kBlob:
        return true;
    default:
        return false;
    }
}

zx_status_t BlobfsCreator::ProcessManifestLine(FILE* manifest, const char* dir_path) {
    char src[PATH_MAX];
    src[0] = '\0';
    char dst[PATH_MAX];
    dst[0] = '\0';

    zx_status_t status;
    if ((status = ParseManifestLine(manifest, dir_path, &src[0], &dst[0])) != ZX_OK) {
        return status;
    }

    if (!strlen(src)) {
        fprintf(stderr, "Manifest line must specify source file\n");
        return ZX_ERR_INVALID_ARGS;
    }

    return ProcessBlob(src);
}

zx_status_t BlobfsCreator::ProcessCustom(int argc, char** argv, uint8_t* processed) {
    constexpr uint8_t required_args = 2;
    if (strcmp(argv[0], "--blob")) {
        fprintf(stderr, "Argument not found: %s\n", argv[0]);
        return ZX_ERR_INVALID_ARGS;
    } else if (argc < required_args) {
        fprintf(stderr, "Not enough arguments for %s\n", argv[0]);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status;
    if ((status = ProcessBlob(argv[1])) != ZX_OK) {
        return status;
    }

    *processed = required_args;
    return ZX_OK;
}

off_t BlobfsCreator::CalculateRequiredSize() {
    blobfs::blobfs_info_t info;
    info.inode_count = blobfs::kBlobfsDefaultInodeCount;
    info.block_count = data_blocks_;
    return (data_blocks_ + blobfs::DataStartBlock(info)) * blobfs::kBlobfsBlockSize;
}

zx_status_t BlobfsCreator::Mkfs() {
    uint64_t block_count;
    if (blobfs::blobfs_get_blockcount(fd_.get(), &block_count)) {
        fprintf(stderr, "blobfs: cannot find end of underlying device\n");
        return ZX_ERR_IO;
    }

    int r = blobfs::blobfs_mkfs(fd_.get(), block_count);

    if (r >= 0 && !blob_list_.is_empty()) {
        zx_status_t status;
        if ((status = Add()) != ZX_OK) {
            return status;
        }
    }
    return r;
}

zx_status_t BlobfsCreator::Fsck() {
    zx_status_t status;
    fbl::unique_ptr<blobfs::Blobfs> vn;
    if ((status = blobfs::blobfs_create(&vn, fbl::move(fd_))) < 0) {
        return status;
    }

    return blobfs::blobfs_check(fbl::move(vn));
}

zx_status_t BlobfsCreator::Add() {
    if (blob_list_.is_empty()) {
        fprintf(stderr, "Adding a blob requires an additional file argument\n");
        return Usage();
    }

    zx_status_t status = ZX_OK;
    fbl::unique_ptr<blobfs::Blobfs> blobfs;
    if ((status = blobfs_create(&blobfs, fbl::move(fd_))) != ZX_OK) {
        return status;
    }

    std::vector<std::thread> threads;
    std::mutex mtx;
    unsigned bi = 0;

    unsigned n_threads = std::thread::hardware_concurrency();
    if (!n_threads) {
        n_threads = 4;
    }
    for (unsigned j = n_threads; j > 0; j--) {
        threads.push_back(std::thread([&] {
            unsigned i = 0;
            while (true) {
                mtx.lock();
                i = bi++;
                mtx.unlock();
                if (i >= blob_list_.size()) {
                    return;
                }
                zx_status_t res;
                if ((res = AppendDepfile(blob_list_[i].c_str())) != ZX_OK) {
                    mtx.lock();
                    status = res;
                    mtx.unlock();
                    return;
                }
                if ((res = AddBlob(blobfs.get(), blob_list_[i].c_str())) < 0) {
                    mtx.lock();
                    status = res;
                    mtx.unlock();
                    return;
                }
            }
        }));
    }

    for (unsigned i = 0; i < threads.size(); i++) {
        threads[i].join();
    }

    return status;
}

zx_status_t BlobfsCreator::ProcessBlob(const char* path) {
    struct stat stats;
    if (stat(path, &stats) < 0) {
        fprintf(stderr, "Failed to stat blob %s\n", path);
        return ZX_ERR_IO;
    }

    zx_status_t status;
    if ((status = ProcessBlocks(stats.st_size)) != ZX_OK) {
        return status;
    }

    blob_list_.push_back(path);
    return ZX_OK;
}

zx_status_t BlobfsCreator::ProcessBlocks(off_t data_size) {
    blobfs::blobfs_inode_t node;
    node.blob_size = data_size;
    data_blocks_ += MerkleTreeBlocks(node) + BlobDataBlocks(node);
    return ZX_OK;
}

int main(int argc, char** argv) {
    BlobfsCreator blobfs;

    if (blobfs.ProcessAndRun(argc, argv) != ZX_OK) {
        return -1;
    }

    return 0;
}
