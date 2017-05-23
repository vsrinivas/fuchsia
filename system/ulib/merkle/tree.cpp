// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <merkle/tree.h>

#include <stdint.h>
#include <string.h>

#include <magenta/assert.h>
#include <magenta/errors.h>
#include <mxalloc/new.h>
#include <merkle/digest.h>
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

} // namespace

////////
// Creation methods

uint64_t Tree::GetTreeLength(uint64_t data_len) {
    uint64_t next_len = NextAligned(data_len);
    return (next_len == 0 ? 0 : next_len + GetTreeLength(next_len));
}

mx_status_t Tree::Create(const void* data, uint64_t data_len, void* tree,
                         uint64_t tree_len, Digest* digest) {
    mx_status_t rc;
    Tree mt;
    if ((rc = mt.CreateInit(data_len, tree_len)) != NO_ERROR ||
        (rc = mt.CreateUpdate(data, data_len, tree)) != NO_ERROR ||
        (rc = mt.CreateFinal(tree, digest)) != NO_ERROR) {
        return rc;
    }
    return NO_ERROR;
}

Tree::Tree()
    : initialized_(false), next_(nullptr), level_(0), offset_(0), length_(0) {}

Tree::~Tree() {}

mx_status_t Tree::CreateInit(uint64_t data_len, uint64_t tree_len) {
    initialized_ = true;
    offset_ = 0;
    length_ = data_len;
    // Data fits in a single node, making this the top level of the tree.
    if (data_len <= kNodeSize) {
        return NO_ERROR;
    }
    AllocChecker ac;
    next_.reset(new (&ac) Tree());
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    next_->level_ = level_ + 1;
    // Ascend the tree.
    data_len = NextAligned(data_len);
    if (tree_len < data_len) {
        return ERR_BUFFER_TOO_SMALL;
    }
    tree_len -= data_len;
    return next_->CreateInit(data_len, tree_len);
}

mx_status_t Tree::CreateUpdate(const void* data, uint64_t length, void* tree) {
    MX_DEBUG_ASSERT(offset_ + length >= offset_);
    // Must call CreateInit first.
    if (!initialized_) {
        return ERR_BAD_STATE;
    }
    // Early exit if no work to do.
    if (length == 0) {
        return NO_ERROR;
    }
    // Must not overrun expected length.
    if (offset_ + length > length_) {
        return ERR_OUT_OF_RANGE;
    }
    // Must have data to read and a tree to fill if expecting more than one
    // digest.
    if (!data || (!tree && length_ > kNodeSize)) {
        return ERR_INVALID_ARGS;
    }
    // Save pointers to the data, digest, and the next level tree.
    const uint8_t* in = static_cast<const uint8_t*>(data);
    uint64_t tree_off = (offset_ - (offset_ % kNodeSize)) / kDigestsPerNode;
    uint8_t* out = static_cast<uint8_t*>(tree) + tree_off;
    void* next = static_cast<uint8_t*>(tree) + NextAligned(length_);
    // Consume the data.
    mx_status_t rc = NO_ERROR;
    while (length > 0 && rc == NO_ERROR) {
        // Check if this is the start of a node.
        if (offset_ % kNodeSize == 0) {
            DigestInit(&digest_, offset_ | level_, length_ - offset_);
        }
        // Hash the node data.
        uint64_t chunk = DigestUpdate(&digest_, in, offset_, length);
        in += chunk;
        offset_ += chunk;
        length -= chunk;
        // Done if not at the end of a node.
        if (offset_ % kNodeSize != 0 && offset_ != length_) {
            break;
        }
        DigestFinal(&digest_, offset_);
        // Done if at the top of the tree.
        if (length_ <= kNodeSize) {
            break;
        }
        // If this is the first digest in a new node, first initialize it.
        if (tree_off % kNodeSize == 0) {
            memset(out, 0, kNodeSize);
        }
        // Add the digest and ascend the tree.
        digest_.CopyTo(out, Digest::kLength);
        rc = next_->CreateUpdate(out, Digest::kLength, next);
        out += Digest::kLength;
        tree_off += Digest::kLength;
    }
    return rc;
}

mx_status_t Tree::CreateFinal(void* tree, Digest* root) {
    return CreateFinalInternal(nullptr, tree, root);
}

mx_status_t Tree::CreateFinalInternal(const void* data, void* tree, Digest* root) {
    // Must call CreateInit first.  Must call CreateUpdate with all data first.
    if (!initialized_ || (level_ == 0 && offset_ != length_)) {
        return ERR_BAD_STATE;
    }
    // Must have root to write and a tree to fill if expecting more than one
    // digest.
    if (!root || (!tree && length_ > kNodeSize)) {
        return ERR_INVALID_ARGS;
    }
    // Special case: the level is empty.
    if (length_ == 0) {
        DigestInit(&digest_, 0, 0);
        DigestFinal(&digest_, 0);
    }
    // Consume padding if needed.
    const uint8_t* tail = static_cast<const uint8_t*>(data) + offset_;
    mx_status_t rc;
    if ((rc = CreateUpdate(tail, length_ - offset_, tree)) != NO_ERROR) {
        return rc;
    }
    initialized_ = false;
    // If the top, save the digest as the Merkle tree root and return.
    if (length_ <= kNodeSize) {
        *root = digest_;
        return NO_ERROR;
    }
    // Finalize the next level up.
    uint8_t* next = static_cast<uint8_t*>(tree) + NextAligned(length_);
    return next_->CreateFinalInternal(tree, next, root);
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
    mx_status_t rc = tree->obj.CreateInit(data_len, tree_len);
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
