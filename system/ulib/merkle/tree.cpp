// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <merkle/tree.h>

#include <string.h>

#include <magenta/errors.h>
#include <mxalloc/new.h>
#include <mxtl/algorithm.h>
#include <mxtl/unique_ptr.h>

namespace merkle {

namespace {

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

constexpr size_t Tree::kNodeSize;
const size_t kDigestsPerNode = Tree::kNodeSize / Digest::kLength;
const size_t kMaxFailures = kDigestsPerNode;

Tree::~Tree() {}

// Public methods

size_t Tree::GetTreeLength(size_t data_len) {
    if (data_len <= kNodeSize) {
        return 0;
    }
    data_len = mxtl::roundup(data_len, kNodeSize);
    MX_DEBUG_ASSERT(data_len != 0);
    size_t total = 0;
    while (data_len > kNodeSize) {
        data_len = mxtl::roundup(data_len / kDigestsPerNode, kNodeSize);
        total += data_len;
    }
    return total;
}

mx_status_t Tree::CreateInit(size_t data_len, void* tree, size_t tree_len) {
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

mx_status_t Tree::CreateUpdate(const void* data, size_t length, void* tree) {
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
        uint64_t locality = static_cast<uint64_t>(offset_ | level_);
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

mx_status_t Tree::Create(const void* data, size_t data_len, void* tree,
                         size_t tree_len, Digest* digest) {
    mx_status_t rc = CreateInit(data_len, tree, tree_len);
    if (rc != NO_ERROR) {
        return rc;
    }
    rc = CreateUpdate(data, data_len, tree);
    if (rc != NO_ERROR) {
        return rc;
    }
    rc = CreateFinal(tree, digest);
    if (rc != NO_ERROR) {
        return rc;
    }
    return NO_ERROR;
}

mx_status_t Tree::SetRanges(size_t data_len, uint64_t offset, size_t length) {
    uint64_t finish = offset + length;
    if (finish < offset || finish > data_len) {
        return ERR_INVALID_ARGS;
    }
    offset -= offset % kNodeSize;
    if (finish != data_len) {
        finish = mxtl::roundup(finish, kNodeSize);
    }
    AllocChecker ac;
    auto raw = new (&ac) Range[offsets_.size()];
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    for (size_t i = 0; i < offsets_.size(); ++i) {
        offset /= kDigestsPerNode;
        offset = offsets_[i] + offset - (offset % kNodeSize);
        raw[i].offset = offset;
        if (length == 0) {
            raw[i].length = 0;
            continue;
        }
        finish /= kDigestsPerNode;
        finish = offsets_[i] + mxtl::roundup(finish, kNodeSize);
        raw[i].offset = offset;
        raw[i].length = static_cast<size_t>(finish - offset);
    }
    ranges_.reset(raw, offsets_.size());
    return NO_ERROR;
}

mx_status_t Tree::Verify(const void* data, size_t data_len, const void* tree,
                         size_t tree_len, uint64_t offset, size_t length,
                         const Digest& digest) {
    num_failures_ = 0;
    data_failures_.reset();
    tree_failures_.reset();
    if ((!data && data_len != 0) || (!tree && tree_len != 0)) {
        return ERR_INVALID_ARGS;
    }
    mx_status_t rc = SetLengths(data_len, tree_len);
    if (rc != NO_ERROR) {
        return rc;
    }
    rc = SetRanges(data_len, offset, length);
    if (rc != NO_ERROR) {
        return rc;
    }
    // Check the root
    level_ = offsets_.size();
    offset_ = level_ == 0 ? 0 : offsets_[level_ - 1];
    HashNode(level_ == 0 ? data : tree);
    if (digest != digest_) {
        AddFailure();
    }
    // Check the Merkle tree.
    const uint8_t* hashes = static_cast<const uint8_t*>(tree);
    uint64_t hash_offset = 0;
    size_t finish = 0;
    while (level_ != 0 && num_failures_ == 0) {
        --level_;
        if (level_ == 0) {
            offset_ = offset - (offset % kNodeSize);
            finish = mxtl::min(mxtl::roundup(offset + length, kNodeSize),
                               static_cast<uint64_t>(data_len));
            hash_offset = (offset_ / kDigestsPerNode);
        } else {
            offset_ = ranges_[level_ - 1].offset;
            finish = offset_ + ranges_[level_ - 1].length;
            hash_offset = offsets_[level_] +
                          (offset_ - offsets_[level_ - 1]) / kDigestsPerNode;
        }
        while (offset_ < finish) {
            HashNode(level_ == 0 ? data : tree);
            if (digest_ != hashes + hash_offset) {
                AddFailure();
            }
            hash_offset += Digest::kLength;
        }
    }
    return VerifyFinal();
}

// Private methods

static_assert(sizeof(size_t) <= sizeof(uint64_t), ">64-bit is unsupported");
mx_status_t Tree::SetLengths(size_t data_len, size_t tree_len) {
    if (tree_len < GetTreeLength(data_len)) {
        return ERR_BUFFER_TOO_SMALL;
    }
    if (data_len == data_len_) {
        return NO_ERROR;
    }
    data_len_ = data_len;
    size_t length = data_len;
    // The tree can't be taller than there are bits in an offset!
    uint64_t offsets[sizeof(uint64_t) * 8] = {0};
    size_t i = 0;
    while (true) {
        size_t nodes = mxtl::roundup(length, kNodeSize) / kNodeSize;
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

mx_status_t Tree::HashData(const void* data, size_t length, void* tree) {
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
        size_t left = static_cast<size_t>(kNodeSize - (offset_ % kNodeSize));
        left = mxtl::min(left, length);
        digest_.Update(bytes, left);
        bytes += left;
        offset_ += left;
        length -= left;
        if (offset_ != data_len_ && offset_ % kNodeSize != 0) {
            break;
        }
        size_t pad_len = kNodeSize - (offset_ % kNodeSize);
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

void Tree::AddFailure() {
    mxtl::Array<uint64_t>* failures =
        (level_ == 0 ? &data_failures_ : &tree_failures_);
    if (num_failures_ == 0) {
        AllocChecker ac;
        auto raw = new (&ac) uint64_t[kMaxFailures];
        if (ac.check()) {
            failures->reset(raw, kMaxFailures);
        }
    }
    if (num_failures_ < failures->size()) {
        (*failures)[num_failures_] =
            mxtl::roundup(offset_, kNodeSize) - kNodeSize;
    }
    ++num_failures_;
}

mx_status_t Tree::VerifyFinal() {
    if (num_failures_ == 0) {
        return NO_ERROR;
    }
    mxtl::Array<uint64_t>* failures = nullptr;
    if (tree_failures_.size() == 0) {
        failures = &data_failures_;
    } else if (data_failures_.size() == 0) {
        failures = &tree_failures_;
    } else {
        return ERR_INTERNAL;
    }
    AllocChecker ac;
    auto raw = new (&ac) uint64_t[num_failures_];
    if (ac.check()) {
        memcpy(raw, failures->get(), num_failures_ * sizeof(uint64_t));
        failures->reset(raw, num_failures_);
    }
    return ERR_IO_DATA_INTEGRITY;
}

} // namespace merkle

// C-style wrapper functions

size_t merkle_tree_length(size_t data_len) {
    merkle::Tree mt;
    return mt.GetTreeLength(data_len);
}

mx_status_t merkle_tree_init(size_t data_len, merkle_tree_t** out) {
    MX_DEBUG_ASSERT(out);
    AllocChecker ac;
    mxtl::unique_ptr<merkle_tree_t> tree(new (&ac) merkle_tree_t());
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    size_t tree_len = tree->obj.GetTreeLength(data_len);
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
                               size_t length) {
    MX_DEBUG_ASSERT(tree && tree->nodes.get());
    return tree->obj.CreateUpdate(data, length, tree->nodes.get());
}

mx_status_t merkle_tree_final(merkle_tree_t* tree, void* out, size_t out_len) {
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

mx_status_t merkle_tree_create(const void* data, size_t data_len, void* tree,
                               size_t tree_len, void* out, size_t out_len) {
    merkle::Tree mt;
    merkle::Digest digest;
    mx_status_t rc = mt.Create(data, data_len, tree, tree_len, &digest);
    if (rc != NO_ERROR) {
        return rc;
    }
    return digest.CopyTo(static_cast<uint8_t*>(out), out_len);
}

mx_status_t merkle_tree_verify(const void* data, size_t data_len, void* tree,
                               size_t tree_len, uint64_t offset, size_t length,
                               const void* root, size_t root_len) {
    if (root_len < merkle::Digest::kLength) {
        return ERR_INVALID_ARGS;
    }
    merkle::Tree mt;
    merkle::Digest digest;
    digest = static_cast<const uint8_t*>(root);
    return mt.Verify(data, data_len, tree, tree_len, offset, length, digest);
}
