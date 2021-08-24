// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "src/lib/fuchsia-cxx/examples/basic/blobstore.h"
#include "src/lib/fuchsia-cxx/examples/basic/src/lib.rs.h"

int main() {
  std::vector<std::string> chunks = {"fuchsia", "is", "cool"};
  auto multi_buf = example::blobstore::new_multi_buf(chunks);

  example::blobstore::BlobstoreClient client;
  auto blobid = client.put(*multi_buf);
  std::cout << "blobid = " << blobid << std::endl;

  client.tag(blobid, "rust");

  auto metadata = client.metadata(blobid);
  std::cout << "tags = [";
  for (std::size_t i = 0; i < metadata.tags.size(); ++i) {
    if (i > 0) {
      std::cout << ", ";
    }
    std::cout << '"' << metadata.tags[i] << '"';
  }
  std::cout << "]" << std::endl;

  return 0;
}
