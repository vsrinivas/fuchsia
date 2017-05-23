// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <merkle/tree.h>

#include <stdint.h>
#include <string.h>

#include <magenta/assert.h>
#include <magenta/errors.h>
#include <mxalloc/new.h>
#include <mxtl/algorithm.h>
#include <mxtl/unique_ptr.h>

namespace merkle {

// Size of a node in bytes.  Defined in tree.h.
constexpr uint64_t Tree::kNodeSize;

// The number of digests that fit in a node.  Importantly, if L is a
// node-aligned length in one level of the Merkle tree, |L / kDigestsPerNode| is
// the corresponding digest-aligned length in the next level up.
const uint64_t kDigestsPerNode = Tree::kNodeSize / Digest::kLength;

namespace {

// Digest wrapper functions.  These functions implement how a node in the Merkle
// tree is hashed:
//    digest = Hash((offset | level) + length + node_data + padding)
// where:
//  * offset is from the start of the VMO.
//  * level is the height of the node in the tree (data nodes have level == 0).
//  * length is the node size, e.g kNodeSize except possibly for the last node.
//  * node_data is the actual bytes from the node.
//  * padding is |kNodeSize - length| zeros.

// Wrapper for Digest::Init.  This primes the working |digest| initializing it
// and hashing the |locality| and |length|.
void DigestInit(Digest* digest, uint64_t locality, uint64_t length) {
    MX_DEBUG_ASSERT(digest);
    MX_DEBUG_ASSERT(length < UINT32_MAX);
    digest->Init();
    digest->Update(&locality, sizeof(locality));
    uint32_t len32 = static_cast<uint32_t>(mxtl::min(length, Tree::kNodeSize));
    digest->Update(&len32, sizeof(len32));
}

// Wrapper for Digest::Update.  This will hash data from |in|, either |length|
// bytes or up to the next node boundary, as determined from |offset|.  Returns
// the number of bytes hashed.
uint64_t DigestUpdate(Digest* digest, const uint8_t* in, uint64_t offset,
                      uint64_t length) {
    MX_DEBUG_ASSERT(digest);
    // Check if length crosses a node boundary
    length = mxtl::min(length, Tree::kNodeSize - (offset % Tree::kNodeSize));
    digest->Update(in, length);
    return length;
}

// Wrapper for Digest::Final.  This pads the hashed data with zeros up to a
// node boundary before finalizing the digest.
void DigestFinal(Digest* digest, uint64_t offset) {
    offset = offset % Tree::kNodeSize;
    if (offset != 0) {
        uint64_t pad_len = Tree::kNodeSize - offset;
        uint8_t pad[pad_len];
        memset(pad, 0, pad_len);
        digest->Update(pad, pad_len);
    }
    digest->Final();
}

////////
// Helper functions for working between levels of the tree.

// Helper function to transform a length in the current level to a length in the
// next level up.
uint64_t NextLength(uint64_t length) {
    if (length > Tree::kNodeSize) {
        return mxtl::roundup(length, Tree::kNodeSize) / kDigestsPerNode;
    } else {
        return 0;
    }
}

// Helper function to transform a length in the current level to a node-aligned
// length in the next level up.
uint64_t NextAligned(uint64_t length) {
    return mxtl::roundup(NextLength(length), Tree::kNodeSize);
}

// TODO(aarongreen): Replace me in the next CL.
uint32_t GetNodeLength(uint64_t offset, uint64_t data_len) {
    MX_DEBUG_ASSERT(offset + Tree::kNodeSize > offset);
    if (offset + Tree::kNodeSize <= data_len) {
        return Tree::kNodeSize;
    }
    if (offset != data_len) {
        return static_cast<uint32_t>(data_len - offset);
    }
    return 0;
}

} // namespace

////////
// Creation methods

uint64_t Tree::GetTreeLength(uint64_t data_len) {
    if (data_len <= kNodeSize) {
        return 0;
    }
    data_len = mxtl::roundup(data_len, kNodeSize);
    MX_DEBUG_ASSERT(data_len != 0);
    uint64_t total = 0;
    while (data_len > kNodeSize) {
        data_len = mxtl::roundup(data_len / kDigestsPerNode, kNodeSize);
        total += data_len;
    }
    return total;
}

mx_status_t Tree::Create(const void* data, uint64_t data_len, void* tree,
                         uint64_t tree_len, Digest* digest) {
    Tree mt;
    mx_status_t rc = mt.CreateInit(data_len, tree, tree_len);
    if (rc != NO_ERROR) {
        return rc;
    }
    rc = mt.CreateUpdate(data, data_len, tree);
    if (rc != NO_ERROR) {
        return rc;
    }
    rc = mt.CreateFinal(tree, digest);
    if (rc != NO_ERROR) {
        return rc;
    }
    return NO_ERROR;
}

Tree::Tree() : data_len_(0), level_(1), offset_(0) {}

Tree::~Tree() {}

mx_status_t Tree::CreateInit(uint64_t data_len, void* tree, uint64_t tree_len) {
    if (!tree && tree_len != 0) {
        return ERR_INVALID_ARGS;
    }
    if (tree_len < GetTreeLength(data_len)) {
        return ERR_BUFFER_TOO_SMALL;
    }
    mx_status_t rc = SetLengths(data_len, tree_len);
    if (rc != NO_ERROR) {
        return rc;
    }
    level_ = 0;
    offset_ = 0;
    memset(tree, 0, tree_len);
    return NO_ERROR;
}

mx_status_t Tree::CreateUpdate(const void* data, uint64_t length, void* tree) {
    if (level_ != 0) {
        return ERR_BAD_STATE;
    }
    if ((!data && length != 0) || (!tree && data_len_ > kNodeSize) ||
        (offset_ + length < offset_)) {
        return ERR_INVALID_ARGS;
    }
    if (offset_ + length > data_len_) {
        return ERR_BUFFER_TOO_SMALL;
    }
    return HashData(data, length, data_len_ < kNodeSize ? nullptr : tree);
}

mx_status_t Tree::CreateFinal(void* tree, Digest* digest) {
    if ((!tree && data_len_ > kNodeSize) || !digest) {
        return ERR_INVALID_ARGS;
    }
    if (offset_ != data_len_ || level_ != 0) {
        return ERR_BAD_STATE;
    }
    if (data_len_ == 0) {
        digest_.Init();
        uint64_t locality = offset_ | level_;
        digest_.Update(&locality, sizeof(locality));
        uint32_t length = 0;
        digest_.Update(&length, sizeof(length));
        digest_.Final();
    }
    if (data_len_ <= kNodeSize) {
        *digest = digest_;
        return NO_ERROR;
    }
    level_ = 1;
    offset_ = 0;
    uint8_t* hash = nullptr;
    if (level_ < offsets_.size()) {
        hash = static_cast<uint8_t*>(tree) + offsets_[1];
    }
    uint8_t* end =
        static_cast<uint8_t*>(tree) + offsets_[offsets_.size() - 1] + kNodeSize;
    while (level_ < offsets_.size()) {
        HashNode(tree);
        mx_status_t rc = digest_.CopyTo(hash, end - hash);
        if (rc != NO_ERROR) {
            return rc;
        }
        hash += Digest::kLength;

        if (offset_ == offsets_[level_]) {
            ++level_;
        }
    }
    HashNode(tree);
    *digest = digest_;
    return NO_ERROR;
}

////////
// Verification methods

mx_status_t Tree::Verify(const void* data, uint64_t data_len, const void* tree,
                         uint64_t tree_len, uint64_t offset, uint64_t length,
                         const Digest& root) {
    uint64_t level = 0;
    uint64_t root_len = data_len;
    while (data_len > kNodeSize) {
        mx_status_t rc;
        // Verify the data in this level.
        if ((rc = VerifyLevel(data, data_len, tree, offset, length, level)) !=
            NO_ERROR) {
            return rc;
        }
        // Ascend to the next level up.
        data = tree;
        root_len = NextLength(data_len);
        data_len = NextAligned(data_len);
        tree = static_cast<const uint8_t*>(tree) + data_len;
        if (tree_len < data_len) {
            return ERR_BUFFER_TOO_SMALL;
        }
        tree_len -= data_len;
        offset /= kDigestsPerNode;
        length /= kDigestsPerNode;
        ++level;
    }
    return VerifyRoot(data, root_len, level, root);
}

mx_status_t Tree::VerifyRoot(const void* data, uint64_t root_len,
                             uint64_t level, const Digest& expected) {
    // Must have data if length isn't 0.  Must have either zero or one node.
    if ((!data && root_len != 0) || root_len > kNodeSize) {
        return ERR_INVALID_ARGS;
    }
    const uint8_t* in = static_cast<const uint8_t*>(data);
    Digest actual;
    // We zero nodes if no data, exactly one node otherwise.
    DigestInit(&actual, level, (root_len == 0 ? 0 : kNodeSize));
    DigestUpdate(&actual, in, 0, root_len);
    DigestFinal(&actual, root_len);
    return (actual == expected ? NO_ERROR : ERR_IO_DATA_INTEGRITY);
}

mx_status_t Tree::VerifyLevel(const void* data, uint64_t data_len,
                              const void* tree, uint64_t offset,
                              uint64_t length, uint64_t level) {
    MX_DEBUG_ASSERT(offset + length >= offset);
    // Must have more than one node of data and digests to check against.
    if (!data || data_len <= kNodeSize || !tree) {
        return ERR_INVALID_ARGS;
    }
    // Must not overrun expected length.
    if (offset + length > data_len) {
        return ERR_OUT_OF_RANGE;
    }
    // Align parameters to node boundaries.
    offset -= offset % kNodeSize;
    length = mxtl::roundup(offset + length, kNodeSize) - offset;
    const uint8_t* in = static_cast<const uint8_t*>(data) + offset;
    // The digests are in the next level up.
    Digest actual;
    const uint8_t* expected =
        static_cast<const uint8_t*>(tree) + (offset / kDigestsPerNode);
    // Check the data of this level against the digests.
    while (length > 0) {
        DigestInit(&actual, offset | level, data_len - offset);
        uint64_t chunk = DigestUpdate(&actual, in, offset, length);
        in += chunk;
        offset += chunk;
        length -= chunk;
        DigestFinal(&actual, offset);
        if (actual != expected) {
            return ERR_IO_DATA_INTEGRITY;
        }
        expected += Digest::kLength;
    }
    return NO_ERROR;
}

////////
// Private helper methods
// TODO(aarongreen): Deprecate and/or move these in the next CL.

mx_status_t Tree::SetLengths(uint64_t data_len, uint64_t tree_len) {
    if (tree_len < GetTreeLength(data_len)) {
        return ERR_BUFFER_TOO_SMALL;
    }
    if (data_len == data_len_) {
        return NO_ERROR;
    }
    data_len_ = data_len;
    uint64_t length = data_len;
    // The tree can't be taller than there are bits in an offset!
    uint64_t offsets[sizeof(uint64_t) * 8] = {0};
    uint64_t i = 0;
    while (true) {
        uint64_t nodes = mxtl::roundup(length, kNodeSize) / kNodeSize;
        if (nodes < 2) {
            break;
        }
        length = mxtl::roundup(nodes * Digest::kLength, kNodeSize);
        if (++i == sizeof(offsets) / sizeof(uint64_t)) {
            return ERR_INTERNAL;
        }
        offsets[i] = offsets[i - 1] + length;
        MX_DEBUG_ASSERT(offsets[i] > offsets[i - 1]);
    }
    if (i == 0) {
        offsets_.reset();
        return NO_ERROR;
    }
    AllocChecker ac;
    auto raw = new (&ac) uint64_t[i];
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    memcpy(raw, offsets, i * sizeof(uint64_t));
    offsets_.reset(raw, i);
    return NO_ERROR;
}

void Tree::HashNode(const void* data) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data) + offset_;
    uint64_t offset = offset_;
    if (offsets_.size() > 0 && level_ != 0) {
        offset -= offsets_[level_ - 1];
    }
    digest_.Init();
    uint64_t locality = offset | level_;
    digest_.Update(&locality, sizeof(locality));
    uint32_t length = GetNodeLength(offset_, data_len_);
    digest_.Update(&length, sizeof(length));
    digest_.Update(bytes, length);
    if (level_ == 0 && length != 0 && length < kNodeSize) {
        uint8_t pad[kNodeSize - length];
        memset(pad, 0, kNodeSize - length);
        digest_.Update(pad, kNodeSize - length);
    }
    offset_ += length;
    digest_.Final();
}

mx_status_t Tree::HashData(const void* data, uint64_t length, void* tree) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint8_t* hashes = static_cast<uint8_t*>(tree);
    uint8_t* end = hashes;
    hashes += (offset_ / kNodeSize) * Digest::kLength;
    end += offsets_.size() > 1 ? offsets_[1] : kNodeSize;
    while (length > 0) {
        if (offset_ % kNodeSize == 0) {
            digest_.Init();
            uint64_t locality = offset_ | level_;
            digest_.Update(&locality, sizeof(locality));
            uint32_t len = GetNodeLength(offset_, data_len_);
            digest_.Update(&len, sizeof(len));
        }
        uint64_t left = kNodeSize - (offset_ % kNodeSize);
        left = mxtl::min(left, length);
        digest_.Update(bytes, left);
        bytes += left;
        offset_ += left;
        length -= left;
        if (offset_ != data_len_ && offset_ % kNodeSize != 0) {
            break;
        }
        uint64_t pad_len = kNodeSize - (offset_ % kNodeSize);
        if (pad_len != kNodeSize) {
            uint8_t pad[pad_len];
            memset(pad, 0, pad_len);
            digest_.Update(pad, pad_len);
        }
        digest_.Final();
        if (!hashes) {
            continue;
        }
        mx_status_t rc = digest_.CopyTo(hashes, end - hashes);
        if (rc != NO_ERROR) {
            return rc;
        }
        hashes += Digest::kLength;
    }
    return NO_ERROR;
}

} // namespace merkle

////////
// C-style wrapper functions

uint64_t merkle_tree_length(uint64_t data_len) {
    merkle::Tree mt;
    return mt.GetTreeLength(data_len);
}

mx_status_t merkle_tree_init(uint64_t data_len, merkle_tree_t** out) {
    MX_DEBUG_ASSERT(out);
    AllocChecker ac;
    mxtl::unique_ptr<merkle_tree_t> tree(new (&ac) merkle_tree_t());
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    uint64_t tree_len = tree->obj.GetTreeLength(data_len);
    tree->nodes.reset(new (&ac) uint8_t[tree_len]);
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    mx_status_t rc =
        tree->obj.CreateInit(data_len, tree->nodes.get(), tree_len);
    if (rc != NO_ERROR) {
        return rc;
    }
    *out = tree.release();
    return NO_ERROR;
}

mx_status_t merkle_tree_update(merkle_tree_t* tree, const void* data,
                               uint64_t length) {
    MX_DEBUG_ASSERT(tree && tree->nodes.get());
    return tree->obj.CreateUpdate(data, length, tree->nodes.get());
}

mx_status_t merkle_tree_final(merkle_tree_t* tree, void* out,
                              uint64_t out_len) {
    MX_DEBUG_ASSERT(tree && tree->nodes.get());
    MX_DEBUG_ASSERT(out);
    merkle::Digest digest;
    mxtl::unique_ptr<merkle_tree_t> owned(tree);
    mx_status_t rc = owned->obj.CreateFinal(owned->nodes.get(), &digest);
    if (rc != NO_ERROR) {
        return rc;
    }
    return digest.CopyTo(static_cast<uint8_t*>(out), out_len);
}

mx_status_t merkle_tree_create(const void* data, uint64_t data_len, void* tree,
                               uint64_t tree_len, void* out, uint64_t out_len) {
    merkle::Tree mt;
    merkle::Digest digest;
    mx_status_t rc = mt.Create(data, data_len, tree, tree_len, &digest);
    if (rc != NO_ERROR) {
        return rc;
    }
    return digest.CopyTo(static_cast<uint8_t*>(out), out_len);
}

mx_status_t merkle_tree_verify(const void* data, uint64_t data_len, void* tree,
                               uint64_t tree_len, uint64_t offset,
                               uint64_t length, const void* root,
                               uint64_t root_len) {
    if (root_len < merkle::Digest::kLength) {
        return ERR_INVALID_ARGS;
    }
    merkle::Tree mt;
    merkle::Digest digest;
    digest = static_cast<const uint8_t*>(root);
    return mt.Verify(data, data_len, tree, tree_len, offset, length, digest);
}
