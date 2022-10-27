// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    fuchsia_fs::file::{AsyncGetSize, AsyncReadAt},
};

/// A struct to open and read a FAR-formatted archive asynchronously.
/// Requires that all paths are valid UTF-8.
#[derive(Debug)]
pub struct AsyncUtf8Reader<T>
where
    T: AsyncReadAt + AsyncGetSize + Unpin,
{
    reader: crate::async_read::AsyncReader<T>,
}

impl<T> AsyncUtf8Reader<T>
where
    T: AsyncReadAt + AsyncGetSize + Unpin,
{
    /// Create a new AsyncUtf8Reader for the provided source.
    pub async fn new(source: T) -> Result<Self, Error> {
        let ret = Self { reader: crate::async_read::AsyncReader::new(source).await? };
        let () = ret.try_list().try_for_each(|r| r.map(|_| ()))?;
        Ok(ret)
    }

    /// Return a list of the items in the archive.
    /// Individual items will error if their paths are not valid UTF-8.
    fn try_list(&self) -> impl ExactSizeIterator<Item = Result<crate::Utf8Entry<'_>, Error>> {
        self.reader.list().map(|e| {
            Ok(crate::Utf8Entry {
                path: std::str::from_utf8(e.path).map_err(|err| Error::PathDataInvalidUtf8 {
                    source: err,
                    path: e.path.into(),
                })?,
                offset: e.offset,
                length: e.length,
            })
        })
    }

    /// Return a list of the items in the archive.
    pub fn list(&self) -> impl ExactSizeIterator<Item = crate::Utf8Entry<'_>> {
        self.try_list().map(|r| {
            r.expect("AsyncUtf8Reader::new only succeeds if try_list succeeds for every element")
        })
    }

    /// Read the entire contents of an entry with the specified path.
    /// O(log(# directory entries))
    pub async fn read_file(&mut self, path: &str) -> Result<Vec<u8>, Error> {
        self.reader.read_file(path.as_bytes()).await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, assert_matches::assert_matches, fuchsia_async as fasync,
        fuchsia_fs::file::Adapter, futures::io::Cursor,
    };

    #[fasync::run_singlethreaded(test)]
    async fn new_rejects_non_utf8_path() {
        let mut far_bytes = vec![];
        let () = crate::write::write(
            &mut far_bytes,
            std::collections::BTreeMap::from_iter([(
                b"\xff",
                (0, Box::new("".as_bytes()) as Box<dyn std::io::Read>),
            )]),
        )
        .unwrap();

        assert_matches!(
            AsyncUtf8Reader::new(Adapter::new(Cursor::new(far_bytes))).await,
            Err(crate::Error::PathDataInvalidUtf8{source: _, path}) if path == b"\xff".to_vec()
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_does_not_panic() {
        let mut far_bytes = vec![];
        let () = crate::write::write(
            &mut far_bytes,
            std::collections::BTreeMap::from_iter([(
                "valid-utf8",
                (0, Box::new("".as_bytes()) as Box<dyn std::io::Read>),
            )]),
        )
        .unwrap();

        itertools::assert_equal(
            AsyncUtf8Reader::new(Adapter::new(Cursor::new(far_bytes))).await.unwrap().list(),
            [crate::Utf8Entry { path: "valid-utf8", offset: 4096, length: 0 }],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_file() {
        let mut far_bytes = vec![];
        let () = crate::write::write(
            &mut far_bytes,
            std::collections::BTreeMap::from_iter([(
                "valid-utf8",
                (12, Box::new("test-content".as_bytes()) as Box<dyn std::io::Read>),
            )]),
        )
        .unwrap();

        assert_eq!(
            AsyncUtf8Reader::new(Adapter::new(Cursor::new(far_bytes)))
                .await
                .unwrap()
                .read_file("valid-utf8")
                .await
                .unwrap(),
            b"test-content".to_vec()
        );
    }
}
