// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_HASHABLE_H_
#define LIB_ESCHER_UTIL_HASHABLE_H_

#include <stdlib.h>

#include "lib/escher/util/hash.h"
#include "lib/fxl/logging.h"

namespace escher {

// A simple base class for self-hashing objects.  Copying and moving is allowed;
// it's up to subclasses to disallow, if they want.
//
// Not thread-safe.
class Hashable {
 public:
  // Return the cached hash, generating it if necessary.
  Hash hash() const {
    if (!hash_.IsValid()) {
      hash_ = GenerateHash();
      FXL_DCHECK(hash_.IsValid()) << "GenerateHash() must return a valid Hash.";
    }
    return hash_;
  }

  // Allows any Hashable object to be used as a HashMap key.
  struct HashMapHasher {
    size_t operator()(const Hashable& key) const { return key.hash().val; }
  };

  bool operator==(const Hashable& other) const = delete;
  bool operator!=(const Hashable& other) const = delete;

 protected:
  // Subclasses must call whenever the object's state changes such that
  // GenerateHash() would return a different result.
  void InvalidateHash() const { hash_ = {0}; }

  // Returns true if there is a cached hash value, i.e. if there has been no
  // call to InvalidateHash() since the last call of GenerateHash().  Mostly for
  // testing.
  bool HasCachedHash() const { return hash_.IsValid(); }

 private:
  // Subclasses must implement to generate a hash value based on the hashable
  // object's internal data.  The generated value must be non-zero.
  virtual Hash GenerateHash() const = 0;

  // Valid hashes are always non-zero.  Zero means invalid.
  mutable Hash hash_ = {0};
};

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_HASHABLE_H_
