// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/111241): Implement production blob API.

pub mod fake {
    use crate::api::Blob as BlobApi;
    use crate::hash::fake::Hash;
    use std::io::Cursor;

    #[derive(Default)]
    pub(crate) struct Blob;

    impl BlobApi for Blob {
        type Hash = Hash;
        type ReaderSeeker = Cursor<Vec<u8>>;

        fn hash(&self) -> Self::Hash {
            Hash::default()
        }

        fn reader_seeker(&self) -> Self::ReaderSeeker {
            Cursor::new(vec![])
        }
    }
}
