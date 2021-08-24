// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Slightly modified version of demo from https://github.com/dtolnay/cxx, adapted to demonstrate
// usage of cxx in the Fuchsia tree and FFI usage from both directions

#ifndef SRC_LIB_FUCHSIA_CXX_EXAMPLES_BASIC_BLOBSTORE_H_
#define SRC_LIB_FUCHSIA_CXX_EXAMPLES_BASIC_BLOBSTORE_H_

#include <memory>

#include "third_party/rust_crates/vendor/cxx/include/cxx.h"

namespace example::blobstore {

struct MultiBuf;
struct BlobMetadata;

class BlobstoreClient {
 public:
  BlobstoreClient();
  uint64_t put(MultiBuf &buf) const;
  void tag(uint64_t blobid, rust::Str tag) const;
  BlobMetadata metadata(uint64_t blobid) const;

 private:
  class impl;
  std::shared_ptr<impl> impl;
};

std::unique_ptr<BlobstoreClient> new_blobstore_client();

}  // namespace example::blobstore

#endif  // SRC_LIB_FUCHSIA_CXX_EXAMPLES_BASIC_BLOBSTORE_H_
