// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/host.h>
#include <fs-host/common.h>

class BlobfsCreator : public FsCreator {
public:
    BlobfsCreator() : FsCreator(blobfs::kStartBlockMinimum) {}

private:
    // Parent overrides:
    zx_status_t Usage() override;
    const char* GetToolName() override { return "blobfs"; }
    bool IsCommandValid(Command command) override;
    bool IsOptionValid(Option option) override;
    bool IsArgumentValid(Argument argument) override;

    zx_status_t ProcessManifestLine(FILE* manifest, const char* dir_path) override;
    zx_status_t ProcessCustom(int argc, char** argv, uint8_t* processed) override;
    off_t CalculateRequiredSize() override;

    //TODO(planders): Add ls support for blobfs.
    zx_status_t Mkfs() override;
    zx_status_t Fsck() override;
    zx_status_t Add() override;

    // Blobfs-specific methods:
    // Add the blob at |path| to the processing list,
    // and calculate the number of blobfs blocks it will require.
    zx_status_t ProcessBlob(const char* path);

    // Calculates the number of minfs data blocks required for a given host-side file size.
    zx_status_t ProcessBlocks(off_t data_size);

    // List of all blobs to be copied into blobfs.
    fbl::Vector<fbl::String> blob_list_;
};
