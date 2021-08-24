// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Slightly modified version of demo from https://github.com/dtolnay/cxx, adapted to demonstrate
// usage of cxx in the Fuchsia tree and FFI usage from both directions

use example_blobstore::{ffi, MultiBuf};

fn main() {
    let client = ffi::new_blobstore_client();

    // Upload a blob.
    let chunks = vec![b"fuchsia".to_vec(), b"is".to_vec(), b"cool".to_vec()];
    let mut buf = MultiBuf::new(chunks);
    let blobid = client.put(&mut buf);
    println!("blobid = {}", blobid);

    // Add a tag.
    client.tag(blobid, "rust");

    // Read back the tags.
    let metadata = client.metadata(blobid);
    println!("tags = {:?}", metadata.tags);
}
