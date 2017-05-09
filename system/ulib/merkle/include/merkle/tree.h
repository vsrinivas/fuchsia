// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <magenta/types.h>

#ifdef __cplusplus
#include <mxtl/array.h>
#include <mxtl/macros.h>
#include <mxtl/unique_ptr.h>

#include <merkle/digest.h>

namespace merkle {

// merkle::Tree represents a hash tree that can be used to independently verify
// subsets of a set data associated with a trusted digest.
//
// A Merkle tree is typically created for a given |data| and |data_len| using
// the following (error checking is omitted):
//      merkle::Tree mt;
//      size_t tree_len = mt.GetTreeLength(data_len);
//      uint8_t *tree = malloc(tree_len); // or other allocation routine
//      merkle::Digest digest;
//      mt.Create(data, data_len, tree, tree_len, &digest);
//
// At this point, |digest| contains the root digest for the Merkle tree
// corresponding to the data. If this digest is trusted (e.g. the creator signs
// it), other parties can use it to verify any portion of the data, chosen by
// |offset| and |length| using the following:
//      merkle::Tree mt;
//      mx_status_t rc =
//          mt.Verify(data, data_len, tree, tree_len, offset, length, digest);
//
// If |s| is NO_ERROR, the |data| between |offset| and |offset + length| is the
// same as when "Create" was called. If it is ERR_IO_DATA_INTEGRITY, either the
// data, tree, or root digest have been altered.  |data_failures| and
// |tree_failures| can be used in those cases to determine the offsets with
// incorrect contents.
class Tree final {
public:
    // This struct is simply used to associate an offset and length when
    // referring to a range of bytes within the data or tree.
    struct Range {
        uint64_t offset;
        size_t length;
    };

    // This sets the size that the tree uses to chunk up the data and digests.
    // TODO(aarongreen): Tune this to optimize performance.
    static constexpr size_t kNodeSize = 8192;

    Tree() : data_len_(0), level_(1), offset_(0), num_failures_(0) {}
    ~Tree();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Tree);

    // After calling |SetRanges|, this will return a list of offsets and length
    // representing the region of the tree that will be accessed when calling
    // Verify with the same |data_len|, |offset|, and |length|.  This can be
    // used to prefetch that data if so desired.
    const mxtl::Array<Range>& ranges() const { return ranges_; }

    // After a call to Verify fails with ERR_IO_DATA_INTEGRITY, these will
    // return the offsets in the data and tree, respectively, that failed to
    // match their parent digests.  The granularity is still |kNodeSize|,
    // meaning for each offset returned the error(s) are in [offset, min(offset+
    // kNodeSize, len)), where len is |data_len| or |tree_len| as appropriate.
    const mxtl::Array<uint64_t>& data_failures() const {
        return data_failures_;
    }
    const mxtl::Array<uint64_t>& tree_failures() const {
        return tree_failures_;
    }

    // Returns the minimum size needed to hold a Merkle tree for the given
    // |data_len|. The tree consists of all the nodes containing the digests of
    // child nodes.  It does NOT include the root digest, which must be passed
    // to |Verify| after a trust decision has been made.  This means that when
    // the |data_len| is less than |kNodeSize|, this method will return 0.
    static size_t GetTreeLength(size_t data_len);

    // Initializes |tree| to hold a the Merkle tree for |data_len| bytes of
    // data.  This must be called before |CreateUpdate|.
    mx_status_t CreateInit(size_t data_len, void* tree, size_t tree_len);

    // Processes an additional |length| bytes of |data| and writes digests to
    // the Merkle |tree|.  It is an error to process more data in total than was
    // specified by |data_len| in |CreateInit|.
    mx_status_t CreateUpdate(const void* data, size_t length, void* tree);

    // Completes the Merkle |tree|, from the data leaves up to the root
    // |digest|, which it writes out.  This must only be called after the total
    // number of bytes processed by |CreateUpdate| equals the |data_len| set by
    // |CreateInit|.
    mx_status_t CreateFinal(void* tree, Digest* digest);

    // Writes a Merkle tree for the given data and saves its root digest.
    // |tree_len| must be at least as much as returned by GetTreeLength().
    mx_status_t Create(const void* data, size_t data_len, void* tree,
                       size_t tree_len, Digest* digest);

    // Sets the range of addresses within the tree that will need to be read to
    // fulfill a corresponding call to Verify. |offset| and |length| must
    // describe a range wholly within |data_len|. If the ranges fail to be set
    // due to low memory, this will return ERR_NO_MEMORY.
    mx_status_t SetRanges(size_t data_len, uint64_t offset, size_t length);

    // Checks the integrity of a the region of data given by the offset and
    // length.  It checks integrity using the given Merkle tree and trusted root
    // digest. |tree_len| must be at least as much as returned by
    // GetTreeLength().  |offset| and |length| must describe a range wholly
    // within |data_len|.
    mx_status_t Verify(const void* data, size_t data_len, const void* tree,
                       size_t tree_len, uint64_t offset, size_t length,
                       const Digest& digest);

private:
    // Sets the length of the data that this Merkle tree references.  This
    // method has the side effect of setting the geometry of the Merkle tree;
    // this can fail due to low memory and return ERR_NO_MEMORY.
    mx_status_t SetLengths(size_t data_len, size_t tree_len);

    // Calculates a digest using the data in |nodes| at given offset |off|.
    // It reads up to |kNodeSize| or until |end|, whichever comes first.  It
    // stores the resulting digest in |out|.  It returns the number of bytes
    // read.
    void HashNode(const void* data);

    // Hashes |length| bytes of |data| that makes up the leaves of the Merkle
    // tree and writes the digests to |tree|.
    mx_status_t HashData(const void* data, size_t length, void* tree);

    // This method adds the given offset |off| to the appropriate list of
    // failures.
    void AddFailure();

    // The method cleans up interim memory used in |Verify| and returns the
    // overall result.
    mx_status_t VerifyFinal();

    // These fields control the overall shape of the tree and its serialization.
    size_t data_len_;
    mxtl::Array<uint64_t> offsets_;

    // These fields are used in walking the tree during creation and/or
    // verification.
    uint64_t level_;
    uint64_t offset_;
    mxtl::Array<Range> ranges_;

    // This field is used as working space when calculating digests.
    Digest digest_;

    // These fields are for checking and tracking bad digests.
    size_t num_failures_;
    mxtl::Array<uint64_t> data_failures_;
    mxtl::Array<uint64_t> tree_failures_;
};

} // namespace merkle
#endif // __cplusplus

#ifndef __cplusplus
typedef struct merkle_tree merkle_tree_t;
#else
typedef struct merkle_tree {
    merkle::Tree obj;
    mxtl::unique_ptr<uint8_t[]> nodes;
} merkle_tree_t;
extern "C" {
#endif // __cplusplus

// C API for merkle::Tree.  |merkle_tree_create| and |merkle_tree_verify| are
// analogous to the corresponding C++ methods and require the caller to handle
// all memory allocations.  |merkle_tree_init|, |merkle_tree_update|, and
// |merkle_tree_final| differ in that they manage the memory needed for the
// Merkle tree object and intermediate nodes transparently.
//
// To create a Merkle tree root digest, a caller can do the following (with
// additional error checking):
//     merkle_tree_t *tree = NULL;
//     merkle_tree_init(data_len, &tree);
//     uint64_t offset = 0;
//     while(offset < data_len) {
//         // Fill buf with len bytes somehow.
//         merkle_tree_update(tree, buf, len);
//         offset += buf_len;
//     }
//     uint8_t digest[MERKLE_DIGEST_LENGTH];
//     merkle_tree_final(&tree, digest, sizeof(digest));
//
// Then |digest| will have the Merkle tree root and |tree| will be null.

// C wrapper for |merkle::Tree::GetTreeLength|.
size_t merkle_tree_length(size_t data_len);

// C wrapper for |merkle::Tree::CreateInit|.  This function allocates memory for
// |tree|, which must be freed by a corresponding call to
// |merkle_tree_final|.
mx_status_t merkle_tree_init(size_t data_len, merkle_tree_t** tree);

// C wrapper function for |merkle::Tree::CreateUpdate|.
mx_status_t merkle_tree_update(merkle_tree_t* tree, const void* data,
                               size_t length);

// C wrapper function for |merkle::Tree::CreateFinal|.  This function consumes
// |tree| and frees it.
mx_status_t merkle_tree_final(merkle_tree_t* tree, void* out, size_t out_len);

// C wrapper function for |merkle::Tree::Create|.
mx_status_t merkle_tree_create(const void* data, size_t data_len, void* tree,
                               size_t tree_len, void* out, size_t out_len);

// C wrapper function for |merkle::Tree::Verify|.
mx_status_t merkle_tree_verify(const void* data, size_t data_len, void* tree,
                               size_t tree_len, uint64_t offset, size_t length,
                               const void* root, size_t root_len);

#ifdef __cplusplus
}
#endif // __cplusplus
