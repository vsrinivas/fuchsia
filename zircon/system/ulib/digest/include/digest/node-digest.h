// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef DIGEST_NODE_DIGEST_H_
#define DIGEST_NODE_DIGEST_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>

namespace digest {

const size_t kMinNodeSize = 512;
const size_t kDefaultNodeSize = 8192;
const size_t kMaxNodeSize = 32768;

// Digest wrapper functions for hashing data organized into "nodes" of a fixed size. The specific
// algorithm is backwards compatible with BlobFS:
//    digest = Hash((id | data_off) + (data_len - data_off) + node_data + padding)
// where:
//  * id is usage-specific (e.g. the tree level when used in a Merkle tree).
//  * data_off is the offset for a specific node.
//  * data_len is the total length of the data.
//  * node_data is the actual bytes from the node.
//  * padding is |kNodeSize - length| zeros.
class NodeDigest {
 public:
  NodeDigest() = default;
  ~NodeDigest() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(NodeDigest);

  const Digest& get() const { return digest_; }
  size_t len() const { return digest_.len(); }
  uint64_t id() const { return id_; }
  size_t node_size() const { return node_size_; }
  void set_id(uint64_t id) { id_ = id; }

  // Sets the node size if |node_size| satisfies |IsValidNodeSize|.
  zx_status_t SetNodeSize(size_t node_size);

  // Returns true if |data_off| is aligned to a node boundary.
  bool IsAligned(size_t data_off) const { return data_off % node_size_ == 0; }

  // Returns the node number for a given |data_off|.
  size_t ToNode(size_t data_off) const { return data_off / node_size_; }

  // Returns the greatest node boundary that is not greater than |data_off|. Returns |data_off| if
  // it is node-aligned.
  size_t PrevAligned(size_t data_off) const { return fbl::round_down(data_off, node_size_); }

  // Returns the smallest node boundary that is not less than |data_off|. Returns |data_off| if it
  // is node-aligned.
  size_t NextAligned(size_t data_off) const { return fbl::round_up(data_off, node_size_); }

  // Returns the largest node-aligned offset.
  size_t MaxAligned() const { return PrevAligned(SIZE_MAX); }

  // Wrapper for Digest::Init.  This primes the working |digest_| initializing it
  // and hashing two values: the "locality", which is the bitwise-XOR of the |id_| and |data_off|,
  // and the "length", which is the |node_size_| or |data_len| - |data-off|, whichever is less.
  zx_status_t Reset(size_t data_off, size_t data_len);

  // Wrapper for Digest::Update.  This will hash data up to |buf_len| bytes from |buf|, and return
  // the number of bytes hashed.
  size_t Append(const void* buf, size_t buf_len);

  // Returns |true| if |node_size| is a power of 2 between |kMinNodeSize| and |kMaxNodeSize|.
  static constexpr bool IsValidNodeSize(size_t node_size) {
    return node_size >= kMinNodeSize && node_size <= kMaxNodeSize && fbl::is_pow2(node_size);
  }

 private:
  // The underlying digest used to hash the data.
  Digest digest_;

  // Number of bytes per node.
  size_t node_size_ = kDefaultNodeSize;

  // Caller-supplied identifier that is mixed into the hash.
  uint64_t id_ = 0;

  // Remaining bytes to consume.
  size_t to_append_ = 0;

  // Length of padding when finalizing the digest.
  size_t pad_len_ = 0;
};

}  // namespace digest

#endif  // DIGEST_NODE_DIGEST_H_
