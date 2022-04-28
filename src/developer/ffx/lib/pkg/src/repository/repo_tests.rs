// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        range::Range,
        repository::{file_system::CHUNK_SIZE, Error},
    },
    assert_matches::assert_matches,
};

#[async_trait::async_trait]
pub(crate) trait TestEnv {
    /// Whether or not the repository backend supports Range requests.
    fn supports_range(&self) -> bool;

    fn write_metadata(&self, path: &str, bytes: &[u8]);

    fn write_blob(&self, path: &str, bytes: &[u8]);

    async fn read_metadata(&self, path: &str, range: Range) -> Result<Vec<u8>, Error>;

    async fn read_blob(&self, path: &str, range: Range) -> Result<Vec<u8>, Error>;
}

// Helper to check that fetching a non-existing file returns a NotFound.
pub(crate) async fn check_fetch_missing(env: &impl TestEnv) {
    assert_matches!(
        env.read_metadata("meta-does-not-exist", Range::Full).await,
        Err(Error::NotFound)
    );
    assert_matches!(env.read_blob("blob-does-not-exist", Range::Full).await, Err(Error::NotFound));
}

// Helper to check that fetching an empty file succeeds.
pub(crate) async fn check_fetch_empty(env: &impl TestEnv) {
    env.write_metadata("empty-meta", b"");
    env.write_blob("empty-blob", b"");

    assert_eq!(env.read_metadata("empty-meta", Range::Full).await.unwrap(), b"");
    assert_eq!(env.read_blob("empty-blob", Range::Full).await.unwrap(), b"");
}

// Helper to check that we can fetch a small file, which fits in a single chunk.
pub(crate) async fn check_fetch_small(env: &impl TestEnv) {
    let meta_body = "hello meta";
    let blob_body = "hello blob";
    env.write_metadata("small-meta", meta_body.as_bytes());
    env.write_blob("small-blob", blob_body.as_bytes());

    let actual = env.read_metadata("small-meta", Range::Full).await.unwrap();
    assert_eq!(String::from_utf8(actual).unwrap(), meta_body);

    let actual = env.read_blob("small-blob", Range::Full).await.unwrap();
    assert_eq!(String::from_utf8(actual).unwrap(), blob_body);
}

// Helper to check that we can fetch a range from a small file, which fits in a single chunk.
pub(crate) async fn check_fetch_range_small(env: &impl TestEnv) {
    let meta_body = "hello meta";
    let blob_body = "hello blob";
    env.write_metadata("small-meta", meta_body.as_bytes());
    env.write_blob("small-blob", blob_body.as_bytes());

    let actual = env
        .read_metadata("small-meta", Range::Inclusive { first_byte_pos: 1, last_byte_pos: 7 })
        .await
        .unwrap();

    assert_eq!(
        String::from_utf8(actual).unwrap(),
        if env.supports_range() { &meta_body[1..=7] } else { &meta_body[..] }
    );

    let actual = env
        .read_blob("small-blob", Range::Inclusive { first_byte_pos: 1, last_byte_pos: 7 })
        .await
        .unwrap();

    assert_eq!(
        String::from_utf8(actual).unwrap(),
        if env.supports_range() { &blob_body[1..=7] } else { &blob_body[..] }
    );
}

// Helper to check that we can fetch a variety of ranges that cross the chunk boundary size.
pub(crate) async fn check_fetch(env: &impl TestEnv) {
    for size in [20, CHUNK_SIZE - 1, CHUNK_SIZE, CHUNK_SIZE + 1, CHUNK_SIZE * 2 + 1] {
        let path = format!("{}", size);
        let body = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();

        env.write_metadata(&path, &body);
        env.write_blob(&path, &body);

        let actual = env.read_metadata(&path, Range::Full).await.unwrap();
        assert_eq!(&actual, &body[..], "size: {size}");

        let actual = env.read_blob(&path, Range::Full).await.unwrap();
        assert_eq!(&actual, &body[..], "size: {size}");
    }
}

pub(crate) async fn check_fetch_range(env: &impl TestEnv) {
    for size in [20, CHUNK_SIZE - 1, CHUNK_SIZE, CHUNK_SIZE + 1, CHUNK_SIZE * 2 + 1] {
        let path = format!("{}", size);
        let body = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();

        env.write_metadata(&path, &body);
        env.write_blob(&path, &body);

        for (range, expected) in [
            (Range::From { first_byte_pos: 0 }, &body[..]),
            (Range::From { first_byte_pos: 5 }, &body[5..]),
            (Range::From { first_byte_pos: size as u64 - 1 }, &body[size - 1..]),
            (Range::Inclusive { first_byte_pos: 0, last_byte_pos: 0 }, &body[0..=0]),
            (Range::Inclusive { first_byte_pos: 0, last_byte_pos: size as u64 - 1 }, &body[..]),
            (Range::Inclusive { first_byte_pos: 5, last_byte_pos: 5 }, &body[5..=5]),
            (Range::Inclusive { first_byte_pos: 5, last_byte_pos: 15 }, &body[5..=15]),
            (
                Range::Inclusive { first_byte_pos: 5, last_byte_pos: size as u64 - 5 },
                &body[5..=size - 5],
            ),
            (
                Range::Inclusive {
                    first_byte_pos: size as u64 - 1,
                    last_byte_pos: size as u64 - 1,
                },
                &body[size - 1..=size - 1],
            ),
            (Range::Suffix { len: 0 }, &[]),
            (Range::Suffix { len: 5 }, &body[size - 5..]),
            (Range::Suffix { len: size as u64 }, &body[..]),
        ] {
            if env.supports_range() {
                let actual = env.read_metadata(&path, range.clone()).await.unwrap();
                assert_eq!(&actual, &expected, "size: {size} range: {range:?}");

                let actual = env.read_blob(&path, range.clone()).await.unwrap();
                assert_eq!(&actual, &expected, "size: {size} range: {range:?}");
            } else {
                println!("{:?} {:?}", range, expected);
                let actual = env.read_metadata(&path, range.clone()).await.unwrap();
                assert_eq!(&actual, &body[..], "size: {size} range: {range:?}");

                let actual = env.read_blob(&path, range.clone()).await.unwrap();
                assert_eq!(&actual, &body[..], "size: {size} range: {range:?}");
            }
        }
    }
}

// Helper to check that fetching an invalid range returns a NotSatisfiable error.
pub(crate) async fn check_fetch_range_not_satisfiable(env: &impl TestEnv) {
    for size in [20, CHUNK_SIZE - 1, CHUNK_SIZE, CHUNK_SIZE + 1, CHUNK_SIZE * 2 + 1] {
        let path = format!("{}", size);
        let body = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();

        env.write_metadata(&path, &body);
        env.write_blob(&path, &body);

        let size = size as u64;
        for range in [
            Range::From { first_byte_pos: size },
            Range::From { first_byte_pos: size + 1 },
            Range::From { first_byte_pos: size + 5 },
            Range::Inclusive { first_byte_pos: 0, last_byte_pos: size },
            Range::Inclusive { first_byte_pos: 5, last_byte_pos: size },
            Range::Inclusive { first_byte_pos: size, last_byte_pos: size },
            Range::Inclusive { first_byte_pos: size, last_byte_pos: size + 5 },
            Range::Inclusive { first_byte_pos: 4, last_byte_pos: 3 },
            Range::Inclusive { first_byte_pos: size + 3, last_byte_pos: size + 5 },
            Range::Suffix { len: size + 1 },
            Range::Suffix { len: size + 5 },
        ] {
            assert_matches!(
                env.read_metadata(&path, range.clone()).await,
                Err(Error::RangeNotSatisfiable),
                "size: {} range: {:?}",
                size,
                range
            );

            assert_matches!(
                env.read_blob(&path, range.clone()).await,
                Err(Error::RangeNotSatisfiable),
                "size: {} range: {:?}",
                size,
                range
            );
        }
    }
}
