// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

#include <magenta/compiler.h>
#include <magenta/types.h>

#ifdef __cplusplus

#include <stdint.h>

#include <digest/digest.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>

namespace digest {

// digest::MerkleTree represents a hash tree that can be used to independently
// verify subsets of a set data associated with a trusted digest.
//
// A Merkle tree is typically created for a given |data| and |data_len| using
// the following (error checking is omitted):
//      size_t tree_len = digest::MerkleTree::GetTreeLength(data_len);
//      uint8_t *tree = malloc(tree_len); // or other allocation routine
//      digest::Digest digest;
//      digest::MerkleTree::Create(data, data_len, tree, tree_len, &digest);
//
// At this point, |digest| contains the root digest for the Merkle tree
// corresponding to the data. If this digest is trusted (e.g. the creator signs
// it), other parties can use it to verify any portion of the data, chosen by
// |offset| and |length| using the following:
//      mx_status_t rc = digest::MerkleTree::Verify(data,
//          data_len, tree, tree_len, offset, length, digest);
//
// If |s| is NO_ERROR, the |data| between |offset| and |offset + length| is the
// same as when "Create" was called. If it is ERR_IO_DATA_INTEGRITY, either the
// data, tree, or root digest have been altered.
class MerkleTree final {
public:
    // This sets the size that the tree uses to chunk up the data and digests.
    // TODO(aarongreen): Tune this to optimize performance.
    static constexpr size_t kNodeSize = 8192;

    // Returns the minimum size needed to hold a Merkle tree for the given
    // |data_len|. The tree consists of all the nodes containing the digests of
    // child nodes.  It does NOT include the root digest, which must be passed
    // to |Verify| after a trust decision has been made.  This means that when
    // the |data_len| is less than |kNodeSize|, this method will return 0.
    static size_t GetTreeLength(size_t data_len);

    // Writes a Merkle tree for the given data and saves its root digest.
    // |tree_len| must be at least as much as returned by GetTreeLength().
    static mx_status_t Create(const void* data, size_t data_len, void* tree,
                              size_t tree_len, Digest* digest);

    // Checks the integrity of a the region of data given by the offset and
    // length.  It checks integrity using the given Merkle tree and trusted root
    // digest. |tree_len| must be at least as much as returned by
    // GetTreeLength().  |offset| and |length| must describe a range wholly
    // within |data_len|.
    static mx_status_t Verify(const void* data, size_t data_len,
                              const void* tree, size_t tree_len, size_t offset,
                              size_t length, const Digest& digest);

    // The stateful instance methods below are only needed when creating a
    // Merkle tree using the Init/Update/Final methods.
    MerkleTree();
    ~MerkleTree();

    // Initializes |tree| to hold a the Merkle tree for |data_len| bytes of
    // data.  This must be called before |CreateUpdate|.
    mx_status_t CreateInit(size_t data_len, size_t tree_len);

    // Processes an additional |length| bytes of |data| and writes digests to
    // the Merkle |tree|.  It is an error to process more data in total than was
    // specified by |data_len| in |CreateInit|.  |tree| must have room for at
    // least |GetTreeLength(data_len)| bytes.
    mx_status_t CreateUpdate(const void* data, size_t length, void* tree);

    // Completes the Merkle |tree|, from the data leaves up to the |root|, which
    // it writes out if not null.  This must only be called after the total
    // number of bytes processed by |CreateUpdate| equals the |data_len| set by
    // |CreateInit|.  |tree| must have room for at least
    // |GetTreeLength(data_len)| bytes.
    mx_status_t CreateFinal(void* tree, Digest* digest);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(MerkleTree);

    // Checks the integrity of the top level of a Merkle tree.  It checks
    // integrity using the given root digest. |data_len| must be at no more than
    // |kNodeSize|.
    static mx_status_t VerifyRoot(const void* data, size_t data_len,
                                  uint64_t level, const Digest& root);

    // Checks the integrity of portion of a Merkle tree level given by the
    // offset and length.  It checks integrity using next level up of the given
    // Merkle tree. |tree_len| must be at least as much as returned by
    // |GetTreeLength(data_len)|.  |offset| and |length| must describe a range
    // wholly within |data_len|.
    static mx_status_t VerifyLevel(const void* data, size_t data_len,
                                   const void* tree, size_t offset,
                                   size_t length, uint64_t level);

    // See CreateFinal.  This implements that method, with an extra parameter to
    // allow levels other than the bottommost to be padded.
    mx_status_t CreateFinalInternal(const void* data, void* tree, Digest* root);

    // All of the following fields are used to save state when creating the
    // Merkle tree using the Init/Update/Final methods.  These methods use a
    // chain of Tree objects, one for each level of the tree.

    // Indicates whether CreateInit has been called without a corresponding call
    // to CreateFinal.
    bool initialized_;

    // For each Tree object in the chain, the Tree object managing the next
    // level up is given by |next_|.
    fbl::unique_ptr<MerkleTree> next_;

    // Indicates the height in the tree of this Tree object, and equals the
    // number of preceding Tree objects in the chain.
    uint64_t level_;

    // Indicates the amount of data consumed so far by |CreateUpdate| for this
    // level.
    size_t offset_;

    // Indicates the total amount of data to be consumed by |CreateUpdate| for
    // this level, as set in |CreateInit|.
    size_t length_;

    // Used to calculate digest, and save the hash state across calls to
    // |CreateUpdate|.
    Digest digest_;
};

} // namespace digest
#endif // __cplusplus

__BEGIN_CDECLS
typedef struct merkle_tree_t merkle_tree_t;

// C API for MerkleTree.  The methods below are directly equivalent to the C++
// methods above, i.e. "merkle_tree_some_method" below would correspond to
// "MerkleTree::SomeMethod" above.  The parameters differ in only two ways:
//      - The stateful creation methods (Init/Update/Final) include a
//        'merkle_tree_t' handle to wrap the Tree object.
//      - Digest arguments have been replace with a void*/size_t pair that
//        indicate the buffer to use to read or save the digest.
//
// The typical flow is similar to using the C++ methods to create a tree is:
//      size_t tree_len = merkle_tree_get_tree_length(data_len);
//      uint8_t *tree = malloc(tree_len);
//      uint8_t *root = malloc(32);
//      return merkle_tree_create(data, data_len, tree, tree_len, out, 32);
//
// An example flow using the stateful creation methods is:
//      size_t tree_len = merkle_tree_get_tree_length(total_data_len);
//      uint8_t *tree = malloc(tree_len);
//      uint8_t root[32];
//      merkle_tree_t *mt;
//      if (merkle_tree_create_init(data_len, tree_len, &mt) != NO_ERROR)
//          return;
//      merkle_tree_create_update(mt, some_data, some_data_len);
//      merkle_tree_create_update(mt, more_data, more_data_len);
//      return merkle_tree_create_final(mt, tree, root, sizeof(root));
//
// The typical flow is similar to using the C++ methods to verify a tree is:
//      return merkle_tree_verify(data, data_len, tree, tree_len, offset,
//                                length, root, sizeof(root));

// C wrapper for |MerkleTree::GetTreeLength|.
size_t merkle_tree_get_tree_length(size_t data_len);

// C wrapper function for |MerkleTree::Create|.
mx_status_t merkle_tree_create(const void* data, size_t data_len, void* tree,
                               size_t tree_len, void* out, size_t out_len);

// C wrapper for |MerkleTree::CreateInit|.  On success, this function
//  allocates memory for |out|.  The caller must free this memory by calling
//  |merkle_tree_create_final|, even if an intervening call to
//  |merkle_tree_create_update| returns an error.
mx_status_t merkle_tree_create_init(size_t data_len, size_t tree_len,
                                    merkle_tree_t** out);

// C wrapper function for |MerkleTree::CreateUpdate|.
mx_status_t merkle_tree_create_update(merkle_tree_t* mt, const void* data,
                                      size_t length, void* tree);

// C wrapper function for |MerkleTree::CreateFinal|.  This function consumes
// |mt| and frees it.
mx_status_t merkle_tree_create_final(merkle_tree_t* mt, void* tree, void* out,
                                     size_t out_len);

// C wrapper function for |MerkleTree::Verify|.
mx_status_t merkle_tree_verify(const void* data, size_t data_len, void* tree,
                               size_t tree_len, size_t offset, size_t length,
                               const void* root, size_t root_len);

__END_CDECLS
