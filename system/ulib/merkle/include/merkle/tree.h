// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
//      uint64_t tree_len = merkle::Tree::GetTreeLength(data_len);
//      uint8_t *tree = malloc(tree_len); // or other allocation routine
//      merkle::Digest digest;
//      merkle::Tree::Create(data, data_len, tree, tree_len, &digest);
//
// At this point, |digest| contains the root digest for the Merkle tree
// corresponding to the data. If this digest is trusted (e.g. the creator signs
// it), other parties can use it to verify any portion of the data, chosen by
// |offset| and |length| using the following:
//      mx_status_t rc = merkle::Tree::Verify(data,
//          data_len, tree, tree_len, offset, length, digest);
//
// If |s| is NO_ERROR, the |data| between |offset| and |offset + length| is the
// same as when "Create" was called. If it is ERR_IO_DATA_INTEGRITY, either the
// data, tree, or root digest have been altered.
class Tree final {
public:
    // This sets the size that the tree uses to chunk up the data and digests.
    // TODO(aarongreen): Tune this to optimize performance.
    static constexpr uint64_t kNodeSize = 8192;

    // Returns the minimum size needed to hold a Merkle tree for the given
    // |data_len|. The tree consists of all the nodes containing the digests of
    // child nodes.  It does NOT include the root digest, which must be passed
    // to |Verify| after a trust decision has been made.  This means that when
    // the |data_len| is less than |kNodeSize|, this method will return 0.
    static uint64_t GetTreeLength(uint64_t data_len);

    // Writes a Merkle tree for the given data and saves its root digest.
    // |tree_len| must be at least as much as returned by GetTreeLength().
    static mx_status_t Create(const void* data, uint64_t data_len, void* tree,
                              uint64_t tree_len, Digest* digest);

    // Checks the integrity of a the region of data given by the offset and
    // length.  It checks integrity using the given Merkle tree and trusted root
    // digest. |tree_len| must be at least as much as returned by
    // GetTreeLength().  |offset| and |length| must describe a range wholly
    // within |data_len|.
    static mx_status_t Verify(const void* data, uint64_t data_len,
                              const void* tree, uint64_t tree_len,
                              uint64_t offset, uint64_t length,
                              const Digest& digest);

    // The stateful instance methods below are only needed when creating a
    // Merkle tree using the Init/Update/Final methods.
    Tree();
    ~Tree();

    // Initializes |tree| to hold a the Merkle tree for |data_len| bytes of
    // data.  This must be called before |CreateUpdate|.
    mx_status_t CreateInit(uint64_t data_len, void* tree, uint64_t tree_len);

    // Processes an additional |length| bytes of |data| and writes digests to
    // the Merkle |tree|.  It is an error to process more data in total than was
    // specified by |data_len| in |CreateInit|.  |tree| must have room for at
    // least |GetTreeLength(data_len)| bytes.
    mx_status_t CreateUpdate(const void* data, uint64_t length, void* tree);

    // Completes the Merkle |tree|, from the data leaves up to the |root|, which
    // it writes out if not null.  This must only be called after the total
    // number of bytes processed by |CreateUpdate| equals the |data_len| set by
    // |CreateInit|.  |tree| must have room for at least
    // |GetTreeLength(data_len)| bytes.
    mx_status_t CreateFinal(void* tree, Digest* digest);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Tree);

    // This struct is simply used to associate an offset and length when
    // referring to a range of bytes within the data or tree.
    struct Range {
        uint64_t offset;
        uint64_t length;
    };

    // Checks the integrity of the top level of a Merkle tree.  It checks
    // integrity using the given root digest.
    mx_status_t VerifyRoot(const void* data, const void* tree, uint64_t* level,
                           const Digest& root);

    // Checks the integrity of portion of a Merkle tree level given by the
    // offset and length.  It checks integrity using next level up of the given
    // Merkle tree. |tree_len| must be at least as much as returned by
    // |GetTreeLength(data_len)|.  |offset| and |length| must describe a range
    // wholly within |data_len|.
    mx_status_t VerifyLevel(const void* data, uint64_t data_len,
                            const void* tree, uint64_t offset, uint64_t length,
                            uint64_t level);

    // Sets the length of the data that this Merkle tree references.  This
    // method has the side effect of setting the geometry of the Merkle tree;
    // this can fail due to low memory and return ERR_NO_MEMORY.
    mx_status_t SetLengths(uint64_t data_len, uint64_t tree_len);

    // Sets the range of addresses within the tree that will need to be read to
    // fulfill a corresponding call to Verify. |offset| and |length| must
    // describe a range wholly within |data_len|. If the ranges fail to be set
    // due to low memory, this will return ERR_NO_MEMORY.
    mx_status_t SetRanges(uint64_t data_len, uint64_t offset, uint64_t length);

    // Calculates a digest using the data in |nodes| at given offset |off|.
    // It reads up to |kNodeSize| or until |end|, whichever comes first.  It
    // stores the resulting digest in |out|.  It returns the number of bytes
    // read.
    void HashNode(const void* data);

    // Hashes |length| bytes of |data| that makes up the leaves of the Merkle
    // tree and writes the digests to |tree|.
    mx_status_t HashData(const void* data, uint64_t length, void* tree);

    // These fields control the overall shape of the tree and its serialization.
    uint64_t data_len_;
    mxtl::Array<uint64_t> offsets_;

    // These fields are used in walking the tree during creation and/or
    // verification.
    uint64_t level_;
    uint64_t offset_;
    mxtl::Array<Range> ranges_;

    // This field is used as working space when calculating digests.
    Digest digest_;
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
uint64_t merkle_tree_length(uint64_t data_len);

// C wrapper for |merkle::Tree::CreateInit|.  This function allocates memory for
// |tree|, which must be freed by a corresponding call to
// |merkle_tree_final|.
mx_status_t merkle_tree_init(uint64_t data_len, merkle_tree_t** tree);

// C wrapper function for |merkle::Tree::CreateUpdate|.
mx_status_t merkle_tree_update(merkle_tree_t* tree, const void* data,
                               uint64_t length);

// C wrapper function for |merkle::Tree::CreateFinal|.  This function consumes
// |tree| and frees it.
mx_status_t merkle_tree_final(merkle_tree_t* tree, void* out, uint64_t out_len);

// C wrapper function for |merkle::Tree::Create|.
mx_status_t merkle_tree_create(const void* data, uint64_t data_len, void* tree,
                               uint64_t tree_len, void* out, uint64_t out_len);

// C wrapper function for |merkle::Tree::Verify|.
mx_status_t merkle_tree_verify(const void* data, uint64_t data_len, void* tree,
                               uint64_t tree_len, uint64_t offset,
                               uint64_t length, const void* root,
                               uint64_t root_len);

#ifdef __cplusplus
}
#endif // __cplusplus
