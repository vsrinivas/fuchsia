// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIGEST_MERKLE_TREE_H_
#define DIGEST_MERKLE_TREE_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <digest/digest.h>
#include <digest/hash-list.h>
#include <fbl/alloc_checker.h>
#include <fbl/macros.h>

namespace digest {
namespace internal {

// |digest::internal::MerkleTree| contains common Merkle tree code. Callers MUST NOT use this
// class directly. See |digest::MerkleTreeCreator| and |digest::MerkleTreeVerifier| below.
template <typename T, typename VP, class MT, class HL>
class MerkleTree {
 public:
  MerkleTree() = default;
  virtual ~MerkleTree() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(MerkleTree);

  size_t GetNodeSize() const { return hash_list_.GetNodeSize(); }
  void SetNodeSize(size_t node_size) { hash_list_.SetNodeSize(); }

  // Returns true if |data_off| is aligned to a node boundary.
  bool IsAligned(size_t data_off) const { return hash_list_.IsAligned(data_off); }

  // Modifies |data_off| and |buf_len| to be aligned to the minimum number of nodes that covered
  // their original range.
  zx_status_t Align(size_t *data_off, size_t *buf_len) const {
    return hash_list_.Align(data_off, buf_len);
  }

  // Sets the length of data this hash list will represent. This will allocate all levels of the
  // Merkle tree, including the root digest.
  zx_status_t SetDataLength(size_t data_len);

  // Returns the minimum size needed to hold a Merkle tree for the given |data_len|. The tree
  // consists of all the nodes containing the digests of child nodes.  It does NOT include the root
  // digest, which must be passed to |Verify| after a trust decision has been made.  This means that
  // when the |data_len| is less than |NodeSize|, this method will return 0.
  size_t GetTreeLength() const;

  // Registers |tree| as a Merkle tree for |data_len_| bytes of data, rooted by a digest of given by
  // |root|.
  zx_status_t SetTree(VP tree, size_t tree_len, VP root, size_t root_len);

 protected:
  // The Merkle tree can be thought of as a singly linked list of HashLists. Each |hash_list_| reads
  // data to produce a list of digests, which in turn becomes the data for the |hash_list_| in the
  // |next_| layer of the tree, until the last layer, which produces the root digest.
  HL hash_list_;
  std::unique_ptr<MT> next_;
};

}  // namespace internal

// |digest::MerkleTreeCreator| creates Merkle trees for data.
// Example (without error checking):
//   MerkleTreeCreator creator;
//   creator.SetDataLength(data_len);
//   size_t tree_len = creator.GetTreeLength();
//   uint8_t *tree = malloc(tree_len); // or other allocation routine
//   uint8_t root[Digest::kLength]; // for storing the resulting root digest
//   creator.SetTree(tree, tree_len, root, sizeof(root));
//   creator.Append(&data[0], partial_len1);
//   creator.Append(&data[partial_len1], partial_len2);
class MerkleTreeCreator
    : public internal::MerkleTree<uint8_t, void *, MerkleTreeCreator, HashListCreator> {
 public:
  // Convenience method to create and return a Merkle tree for the given |data| via |out_tree| and
  // |out_root|.
  static zx_status_t Create(const void *data, size_t data_len, std::unique_ptr<uint8_t[]> *out_tree,
                            size_t *out_tree_len, Digest *out_root);

  // Reads |buf_len| bytes of data from |buf| and appends digests to the hash |list|.
  zx_status_t Append(const void *buf, size_t buf_len);
};

// |digest::MerkleTreeVerifier| verifies data against a Merkle tree.
// Example (without error checking):
//   MerkleTreeVerifier verifier;
//   verifier.SetDataLength(data_len);
//   verifier.SetTree(tree, tree_len, root.get(), root.len());
//   verifier.Align(&data_off, &partial_len);
//   return verifier.Verify(&data[data_off], partial_len) == ZX_OK;
class MerkleTreeVerifier : public internal::MerkleTree<const uint8_t, const void *,
                                                       MerkleTreeVerifier, HashListVerifier> {
 public:
  // Convenience method to verify the integrity of the node-aligned |buf| at |data_off| using the
  // Merkle |tree| and |root|.
  static zx_status_t Verify(const void *buf, size_t buf_len, size_t data_off, size_t data_len,
                            const void *tree, size_t tree_len, const Digest &root);

  // Reads |buf_len| bytes of data from |buf|, calculates digests for each node of data, and
  // compares them to the digests stored in the Merkle tree. |data_off| must be node-aligned.
  // |buf_len| must be node-aligned, or reach the end of the data. See also |Align|.
  zx_status_t Verify(const void *buf, size_t buf_len, size_t data_off);
};

// Convenience method for calculating the minimum size needed to hold a Merkle tree for the given
// |data_size|.  It does NOT include room for the root digest.
//
// Panics if |node_size| does not satisfy |NodeDigest::IsValidNodeSize|.
size_t CalculateMerkleTreeSize(size_t data_size, size_t node_size);

}  // namespace digest

#endif  // DIGEST_MERKLE_TREE_H_
