// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef DIGEST_HASH_LIST_H_
#define DIGEST_HASH_LIST_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <digest/digest.h>
#include <digest/node-digest.h>

namespace digest {
namespace internal {

// |digest::internal::HashListBase| contains common hash list code. Callers MUST NOT use this class
// directly. See |digest::HashListCreator| and |digest::HashListVerifier| below.
class HashListBase {
 public:
  HashListBase() = default;
  virtual ~HashListBase() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(HashListBase);

  size_t data_off() const { return data_off_; }
  size_t data_len() const { return data_len_; }
  size_t list_off() const { return list_off_; }
  size_t list_len() const { return list_len_; }

  // Gets or sets relevant NodeDigest fields.
  uint64_t GetNodeId() const { return node_digest_.id(); }
  size_t GetNodeSize() const { return node_digest_.node_size(); }
  size_t GetDigestSize() const { return node_digest_.len(); }
  void SetNodeId(uint64_t id) { node_digest_.set_id(id); }
  zx_status_t SetNodeSize(size_t node_size) { return node_digest_.SetNodeSize(node_size); }

  // Returns true if |data_off| is aligned to a node boundary.
  bool IsAligned(size_t data_off) const { return node_digest_.IsAligned(data_off); }

  // Modifies |data_off| and |buf_len| to be aligned to the minimum number of nodes that covered
  // their original range.
  zx_status_t Align(size_t *data_off, size_t *buf_len) const;

  // Sets the length of data this hash list will represent. The maxium possible size is
  // |SIZE_MAX - NodeSize + 1|, i.e. the maximum node-aligned value of type |size_t|.
  zx_status_t SetDataLength(size_t data_len);

  // Returns the corresponding offset in the hash list for an offset in the data. This method
  // does not check if |data_off| is within bounds.
  size_t GetListOffset(size_t data_off) const;

  // Returns the minimum size needed to hold a hash list for the given |data_len|. Note that this
  // differs from |list_len()| in that it returns what's needed, whereas the latter returns what
  // the list length currently is.
  size_t GetListLength() const;

 protected:
  void set_list_len(size_t list_len) { list_len_ = list_len; }

  // Checks range given by |data_off| and |buf_len| is valid.
  virtual bool IsValidRange(size_t data_off, size_t buf_len);

  // Handle the |buf_len| bytes from |buf|, corresponding to the data sequence starting at
  // |data_off|.
  zx_status_t ProcessData(const uint8_t *buf, size_t buf_len, size_t data_off);

  // Process the next digest in the hash list, e.g. write a digest when creating, or compare when
  // verifying. The no-argument version simply invokes the second version with the current digest.
  // The second version is implemented in derived classes.
  void HandleOne();
  virtual void HandleOne(const Digest &digest) {}

 private:
  zx_status_t Check(size_t off, size_t len, size_t max, size_t *out = nullptr) const;

  // Digest object used to create hashes to store or check.
  NodeDigest node_digest_;

  // Offset and length of data represented by the hash list.
  size_t data_off_ = 0;
  size_t data_len_ = 0;

  // Contents, offset, and length of the hash list.
  size_t list_off_ = 0;
  size_t list_len_ = 0;
};

// |digest::internal::HashList| contains code templated on the list type. Callers MUST NOT use this
// class directly. See |digest::HashListCreator| and |digest::HashListVerifier| below.
template <typename T>
class HashList : public HashListBase {
 public:
  static_assert(sizeof(T) == sizeof(uint8_t), "Do not invoke HashList directly.");

  T *list() const { return list_; }

  // Registers |list| as a hash list for |data_len_| bytes of data.
  zx_status_t SetList(T *list, size_t list_len);

 private:
  T *list_ = nullptr;
};

}  // namespace internal

// |digest::HashListCreator| creates hash lists for data.
// Example (without error checking):
//   HashListCreator creator;
//   creator.SetDataLength(data_len);
//   size_t list_len = creator.GetListLength();
//   uint8_t *list = malloc(list_len); // or other allocation routine
//   creator.SetList(list, list_len);
//   creator.Append(&data[0], partial_len1);
//   creator.Append(&data[partial_len1], partial_len2);
class HashListCreator : public internal::HashList<uint8_t> {
 public:
  // Reads |buf_len| bytes of data from |buf| and appends digests to the hash |list|.
  zx_status_t Append(const void *buf, size_t buf_len);

 protected:
  // Writes a single calculated digest to the appropriate position in the list.
  void HandleOne(const Digest &digest) override;
};

// |digest::HashListVerifier| verifies data against a hash list.
// Example (without error checking):
//   HashListVerifier verifier;
//   verifier.SetDataLength(data_len);
//   verifier.SetList(list, list_len);
//   verifier.Align(&data_off, &partial_len);
//   return verifier.Verify(&data[data_off], partial_len) == ZX_OK;
class HashListVerifier : public internal::HashList<const uint8_t> {
 public:
  // Reads |buf_len| bytes of data from |buf|, calculates digests for each node of data, and
  // compares them to the digests stored in the hash list. |data_off| must be node-aligned.
  // |buf_len| must be node-aligned, or reach the end of the data. See also |Align|.
  zx_status_t Verify(const void *buf, size_t buf_len, size_t data_off);

 protected:
  // Verification ranges must start on a node boundary, and end on a node boundary or the end of the
  // data.
  bool IsValidRange(size_t data_off, size_t buf_len) override;

  // Compares a single calculated digest to the corresponding one in the list.
  void HandleOne(const Digest &digest) override;

 private:
  // Used to store the verification result. The verification logic intentionally does NOT short
  // circuit; we want the hash checks to be as close to constant time as possible.
  bool verified_ = false;
};

// Convenience method for calculating the minimum size needed to hold a hash list for the given
// |data_size|.
//
// Panics if |node_size| does not satisfy |NodeDigest::IsValidNodeSize|.
size_t CalculateHashListSize(size_t data_size, size_t node_size);

}  // namespace digest

#endif  // DIGEST_HASH_LIST_H_
