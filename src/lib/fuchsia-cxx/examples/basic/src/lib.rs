// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Slightly modified version of demo from https://github.com/dtolnay/cxx, adapted to demonstrate
// usage of cxx in the Fuchsia tree and FFI usage from both directions

// TODO(fxbug.rev/81940): This lint must currently be disabled if your bridge uses UniquePtr.
#![allow(elided_lifetimes_in_paths)]

use cxx::{CxxString, CxxVector};

#[cxx::bridge(namespace = "example::blobstore")]
pub mod ffi {
    // Shared structs with fields visible to both languages.
    pub struct BlobMetadata {
        size: usize,
        tags: Vec<String>,
    }

    // Rust types and signatures exposed to C++.
    extern "Rust" {
        type MultiBuf;

        fn new_multi_buf(chunks: &CxxVector<CxxString>) -> Box<MultiBuf>;
        fn next_chunk(buf: &mut MultiBuf) -> &[u8];
    }

    // C++ types and signatures exposed to Rust.
    //
    // Note: The "unsafe" here indicates that we're declaring the functions inside this block as
    // safe to call from Rust. CXX uses static assertions to check many things, like that these
    // signatures match, but C++ usage from Rust is inherently Rust-unsafe in other ways (e.g. UB is
    // not statically prevented) and this "unsafe" declaration indicates that the programmer is
    // taking responsibility for those things. See
    // https://cxx.rs/extern-c++.html#functions-and-member-functions for more info.
    unsafe extern "C++" {
        include!("src/lib/fuchsia-cxx/examples/basic/blobstore.h");

        type BlobstoreClient;

        // TODO(fxbug.rev/81940): See above; UniquePtr usage in the bridge currently requires
        // disabling the `allow_lifetimes_in_paths` lint at the crate level.
        pub fn new_blobstore_client() -> UniquePtr<BlobstoreClient>;
        pub fn put(&self, parts: &mut MultiBuf) -> u64;
        pub fn tag(&self, blobid: u64, tag: &str);
        pub fn metadata(&self, blobid: u64) -> BlobMetadata;
    }
}

// An iterator over contiguous chunks of a discontiguous file object.
//
// Toy implementation uses a Vec<Vec<u8>> but in reality this might be iterating
// over some more complex Rust data structure like a rope, or maybe loading
// chunks lazily from somewhere.
pub struct MultiBuf {
    chunks: Vec<Vec<u8>>,
    pos: usize,
}

impl MultiBuf {
    pub fn new(chunks: Vec<Vec<u8>>) -> MultiBuf {
        MultiBuf { chunks, pos: 0 }
    }
}

pub fn new_multi_buf(chunks: &CxxVector<CxxString>) -> Box<MultiBuf> {
    let chunks = chunks.iter().map(|s| Vec::from(s.as_bytes())).collect();
    Box::new(MultiBuf { chunks, pos: 0 })
}

pub fn next_chunk(buf: &mut MultiBuf) -> &[u8] {
    let next = buf.chunks.get(buf.pos);
    buf.pos += 1;
    next.map_or(&[], Vec::as_slice)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_ffi_from_rust() {
        let client = ffi::new_blobstore_client();

        // Upload a blob.
        let chunks = vec![b"fuchsia".to_vec(), b"is".to_vec(), b"cool".to_vec()];
        let mut buf = MultiBuf::new(chunks);
        let blobid = client.put(&mut buf);

        // Add a tag.
        client.tag(blobid, "rust");

        // Read back the metadata and check that it is as expected.
        let metadata = client.metadata(blobid);
        assert_eq!(metadata.tags.as_slice(), &["rust"]);
        assert_eq!(metadata.size, 13);

        // Check that the metadata for a non-existent blob is empty.
        let metadata = client.metadata(blobid + 1);
        assert!(metadata.tags.is_empty());
        assert_eq!(metadata.size, 0);
    }
}
