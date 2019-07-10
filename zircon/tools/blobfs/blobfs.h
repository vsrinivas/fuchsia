// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include <blobfs/host.h>
#include <digest/digest.h>
#include <fbl/array.h>
#include <fbl/vector.h>
#include <fs-host/common.h>
#include <lib/fit/defer.h>

class BlobfsCreator : public FsCreator {
public:
    BlobfsCreator()
        : FsCreator(blobfs::kMinimumDataBlocks) {}

private:
    // Parent overrides:
    zx_status_t Usage() override;
    const char* GetToolName() override { return "blobfs"; }
    bool IsCommandValid(Command command) override;
    bool IsOptionValid(Option option) override;
    bool IsArgumentValid(Argument argument) override;

    // Identify blobs to be operated on, populating the internal
    // |blob_list_|.
    zx_status_t ProcessManifestLine(FILE* manifest, const char* dir_path) override;
    zx_status_t ProcessCustom(int argc, char** argv, uint8_t* processed) override;

    // Calculates merkle trees for the processed blobs, and determines
    // the total size of the underlying storage necessary to contain them.
    zx_status_t CalculateRequiredSize(off_t* out) override;

    //TODO(planders): Add ls support for blobfs.
    zx_status_t Mkfs() override;
    zx_status_t Fsck() override;
    zx_status_t UsedDataSize() override;
    zx_status_t UsedInodes() override;
    zx_status_t UsedSize() override;
    zx_status_t Add() override;

    // A comparison function used to quickly compare MerkleInfo.
    struct DigestCompare {
        inline bool operator()(const blobfs::MerkleInfo& lhs, const blobfs::MerkleInfo& rhs) const {
            const uint8_t* lhs_bytes = lhs.digest.AcquireBytes();
            const uint8_t* rhs_bytes = rhs.digest.AcquireBytes();
            auto auto_release = fit::defer([&]() {
                lhs.digest.ReleaseBytes();
                rhs.digest.ReleaseBytes();
            });

            for (size_t i = 0; i < digest::Digest::kLength; i++) {
                if (lhs_bytes[i] < rhs_bytes[i]) {
                    return true;
                }
            }
            return false;
        }
    };

    // List of all blobs to be copied into blobfs.
    fbl::Vector<fbl::String> blob_list_;

    // A list of Merkle Information for blobs in |blob_list_|.
    std::vector<blobfs::MerkleInfo> merkle_list_;
};
