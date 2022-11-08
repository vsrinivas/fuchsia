// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/111241): Implement production blob API.

#[cfg(test)]
pub mod fake {
    use crate::api::Blob as BlobApi;
    use crate::data_source::fake::DataSource;
    use crate::hash::fake::Hash;
    use std::io::Cursor;
    use thiserror::Error;

    #[derive(Clone, Debug, Eq, Error, PartialEq)]
    pub(crate) enum BlobError {
        #[error("fake blob error")]
        BlobError,
    }

    pub(crate) struct Blob {
        hash: Hash,
        blob: Result<Cursor<Vec<u8>>, BlobError>,
        data_sources: Vec<DataSource>,
    }

    impl Default for Blob {
        fn default() -> Self {
            Self {
                hash: Hash::default(),
                blob: Ok(Cursor::new(vec![])),
                data_sources: vec![DataSource::default()],
            }
        }
    }

    impl Blob {
        pub fn new(hash: Hash, blob: Vec<u8>) -> Self {
            Self { hash, blob: Ok(Cursor::new(blob)), data_sources: vec![DataSource::default()] }
        }

        pub fn new_error(hash: Hash) -> Self {
            Self {
                hash,
                blob: Err(BlobError::BlobError),
                data_sources: vec![DataSource::default()],
            }
        }

        pub fn new_with_data_sources(
            hash: Hash,
            blob: Vec<u8>,
            data_sources: Vec<DataSource>,
        ) -> Self {
            Self { hash, blob: Ok(Cursor::new(blob)), data_sources }
        }

        pub fn new_error_with_data_sources(hash: Hash, data_sources: Vec<DataSource>) -> Self {
            Self { hash, blob: Err(BlobError::BlobError), data_sources: data_sources }
        }
    }

    impl BlobApi for Blob {
        type Hash = Hash;
        type ReaderSeeker = Cursor<Vec<u8>>;
        type DataSource = DataSource;
        type Error = BlobError;

        fn hash(&self) -> Self::Hash {
            self.hash
        }

        fn reader_seeker(&self) -> Result<Self::ReaderSeeker, Self::Error> {
            self.blob.clone()
        }

        fn data_sources(&self) -> Box<dyn Iterator<Item = Self::DataSource>> {
            Box::new(self.data_sources.clone().into_iter())
        }
    }

    mod test {
        use super::Blob;
        use super::BlobError;
        use crate::api::Blob as BlobApi;
        use crate::data_source::fake::DataSource;

        #[fuchsia::test]
        fn test_new() {
            let (a, b, c) = (Blob::new(0, vec![]), Blob::new(0, vec![]), Blob::new(1, vec![]));
            assert_eq!(a.hash(), b.hash());
            assert!(a.hash() != c.hash());
            let v = vec![a, b, c];
            for x in v.into_iter() {
                assert!(x.reader_seeker().is_ok());
                let ds = x.data_sources().collect::<Vec<DataSource>>();
                assert_eq!(ds.len(), 1);
                assert_eq!(ds[0], DataSource);
            }
        }

        #[fuchsia::test]
        fn test_new_error() {
            let (a, b, c) = (Blob::new_error(0), Blob::new_error(0), Blob::new_error(1));
            assert_eq!(a.hash(), b.hash());
            assert!(a.hash() != c.hash());
            let v = vec![a, b, c];
            for x in v.into_iter() {
                assert_eq!(x.reader_seeker(), Err(BlobError::BlobError));
                let ds = x.data_sources().collect::<Vec<DataSource>>();
                assert_eq!(ds.len(), 1);
                assert_eq!(ds[0], DataSource);
            }
        }

        #[fuchsia::test]
        fn test_new_with_data_sources() {
            let ds_a = vec![];
            let ds_b = vec![DataSource];
            let ds_c = vec![DataSource, DataSource];
            let (a, b, c) = (
                Blob::new_with_data_sources(0, vec![], ds_a.clone()),
                Blob::new_with_data_sources(0, vec![], ds_b.clone()),
                Blob::new_with_data_sources(1, vec![], ds_c.clone()),
            );
            assert_eq!(a.hash(), b.hash());
            assert!(a.hash() != c.hash());

            let ds = a.data_sources().collect::<Vec<DataSource>>();
            assert_eq!(ds, ds_a);

            let ds = b.data_sources().collect::<Vec<DataSource>>();
            assert_eq!(ds, ds_b);

            let ds = c.data_sources().collect::<Vec<DataSource>>();
            assert_eq!(ds, ds_c);

            let v = vec![a, b, c];
            for x in v.iter() {
                assert!(x.reader_seeker().is_ok());
            }
        }

        #[fuchsia::test]
        fn test_new_with_error_with_data_sources() {
            let ds_a = vec![];
            let ds_b = vec![DataSource];
            let ds_c = vec![DataSource, DataSource];
            let (a, b, c) = (
                Blob::new_error_with_data_sources(0, ds_a.clone()),
                Blob::new_error_with_data_sources(0, ds_b.clone()),
                Blob::new_error_with_data_sources(1, ds_c.clone()),
            );
            assert_eq!(a.hash(), b.hash());
            assert!(a.hash() != c.hash());

            let ds = a.data_sources().collect::<Vec<DataSource>>();
            assert_eq!(ds, ds_a);

            let ds = b.data_sources().collect::<Vec<DataSource>>();
            assert_eq!(ds, ds_b);

            let ds = c.data_sources().collect::<Vec<DataSource>>();
            assert_eq!(ds, ds_c);

            let v = vec![a, b, c];
            for x in v.iter() {
                assert_eq!(x.reader_seeker(), Err(BlobError::BlobError));
            }
        }
    }
}
