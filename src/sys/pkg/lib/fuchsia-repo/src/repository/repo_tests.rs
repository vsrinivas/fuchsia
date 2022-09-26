// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    range::Range,
    repository::{Error, RepoProvider},
};

#[async_trait::async_trait]
pub(crate) trait TestEnv: Sized {
    /// Whether or not the repository backend supports Range requests.
    fn supports_range(&self) -> bool;

    /// Write a test metadata at this path with these bytes.
    fn write_metadata(&self, path: &str, bytes: &[u8]);

    /// Write a test blob at this path with these bytes.
    fn write_blob(&self, path: &str, bytes: &[u8]);

    /// The repository under test.
    fn repo(&self) -> &dyn RepoProvider;

    /// Shut down the test environment.
    async fn stop(self) {}
}

/// Helper to read a metadata resource into a vector.
pub(crate) async fn read_metadata(
    env: &impl TestEnv,
    path: &str,
    range: Range,
) -> Result<Vec<u8>, Error> {
    let mut body = vec![];
    let mut resource = env.repo().fetch_metadata_range(path, range).await?;
    resource.read_to_end(&mut body).await?;

    assert_eq!(resource.content_len(), body.len() as u64);

    if range == Range::Full {
        assert_eq!(resource.total_len(), body.len() as u64);
    } else {
        assert!(body.len() as u64 <= resource.total_len());
    }

    Ok(body)
}

/// Helper to read a blob resource into a vector.
pub(crate) async fn read_blob(
    env: &impl TestEnv,
    path: &str,
    range: Range,
) -> Result<Vec<u8>, Error> {
    let mut body = vec![];
    let mut resource = env.repo().fetch_blob_range(path, range).await?;
    resource.read_to_end(&mut body).await?;

    assert_eq!(resource.content_len(), body.len() as u64);

    if range == Range::Full {
        assert_eq!(resource.total_len(), body.len() as u64);
    } else {
        assert!(body.len() as u64 <= resource.total_len());
    }

    Ok(body)
}

macro_rules! repo_test_suite {
    (
        env = $create_env:expr;
        chunk_size = $chunk_size:expr;
    ) => {
        #[cfg(test)]
        mod repo_test_suite {
            use {
                super::*,
                assert_matches::assert_matches,
                $crate::{
                    range::Range,
                    repository::{
                        repo_tests::{read_blob, read_metadata},
                        Error,
                    },
                },
            };

            // Test to check that fetching a non-existing file returns a NotFound.
            #[fuchsia_async::run_singlethreaded(test)]
            async fn test_fetch_missing() {
                let env = $create_env;

                assert_matches!(
                    read_metadata(&env, "meta-does-not-exist", Range::Full).await,
                    Err(Error::NotFound)
                );
                assert_matches!(
                    read_blob(&env, "blob-does-not-exist", Range::Full).await,
                    Err(Error::NotFound)
                );

                env.stop().await;
            }

            // Test to check that fetching an empty file succeeds.
            #[fuchsia_async::run_singlethreaded(test)]
            async fn test_fetch_empty() {
                let env = $create_env;

                env.write_metadata("empty-meta", b"");
                env.write_blob("empty-blob", b"");

                assert_eq!(read_metadata(&env, "empty-meta", Range::Full).await.unwrap(), b"");
                assert_eq!(read_blob(&env, "empty-blob", Range::Full).await.unwrap(), b"");

                env.stop().await;
            }

            // Test to check that we can fetch a small file, which fits in a single chunk.
            #[fuchsia_async::run_singlethreaded(test)]
            async fn test_fetch_small() {
                let env = $create_env;

                let meta_body = "hello meta";
                let blob_body = "hello blob";
                env.write_metadata("small-meta", meta_body.as_bytes());
                env.write_blob("small-blob", blob_body.as_bytes());

                let actual = read_metadata(&env, "small-meta", Range::Full).await.unwrap();
                assert_eq!(String::from_utf8(actual).unwrap(), meta_body);

                let actual = read_blob(&env, "small-blob", Range::Full).await.unwrap();
                assert_eq!(String::from_utf8(actual).unwrap(), blob_body);

                env.stop().await;
            }

            // Test to check that we can fetch a range from a small file, which fits in a single chunk.
            #[fuchsia_async::run_singlethreaded(test)]
            async fn test_fetch_range_small() {
                let env = $create_env;

                let meta_body = "hello meta";
                let blob_body = "hello blob";
                env.write_metadata("small-meta", meta_body.as_bytes());
                env.write_blob("small-blob", blob_body.as_bytes());

                let actual = read_metadata(
                    &env,
                    "small-meta",
                    Range::Inclusive { first_byte_pos: 1, last_byte_pos: 7 },
                )
                .await
                .unwrap();

                assert_eq!(
                    String::from_utf8(actual).unwrap(),
                    if env.supports_range() { &meta_body[1..=7] } else { &meta_body[..] }
                );

                let actual = read_blob(
                    &env,
                    "small-blob",
                    Range::Inclusive { first_byte_pos: 1, last_byte_pos: 7 },
                )
                .await
                .unwrap();

                assert_eq!(
                    String::from_utf8(actual).unwrap(),
                    if env.supports_range() { &blob_body[1..=7] } else { &blob_body[..] }
                );

                env.stop().await;
            }

            // Test to check that we can fetch a variety of ranges that cross the chunk boundary size.
            #[fuchsia_async::run_singlethreaded(test)]
            async fn test_fetch() {
                let env = $create_env;

                for size in [20, $chunk_size - 1, $chunk_size, $chunk_size + 1, $chunk_size * 2 + 1]
                {
                    let path = format!("{}", size);
                    let body = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();

                    env.write_metadata(&path, &body);
                    env.write_blob(&path, &body);

                    let actual = read_metadata(&env, &path, Range::Full).await.unwrap();
                    assert_eq!(&actual, &body[..], "size: {size}");

                    let actual = read_blob(&env, &path, Range::Full).await.unwrap();
                    assert_eq!(&actual, &body[..], "size: {size}");
                }

                env.stop().await;
            }

            #[fuchsia_async::run_singlethreaded(test)]
            async fn test_fetch_range() {
                // We have a number of test cases, so we'll break them up into multiple concurrent
                // tests.

                let env = $create_env;

                let mut size_cases = vec![];
                for size in [20, $chunk_size - 1, $chunk_size, $chunk_size + 1, $chunk_size * 2 + 1]
                {
                    let path = format!("{}", size);
                    let body = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();

                    env.write_metadata(&path, &body);
                    env.write_blob(&path, &body);

                    size_cases.push((size, path, body));
                }

                let mut test_cases = vec![];
                for (size, path, body) in &size_cases {
                    let size = *size;

                    for (range, expected) in [
                        (Range::From { first_byte_pos: 0 }, &body[..]),
                        (Range::From { first_byte_pos: 5 }, &body[5..]),
                        (Range::From { first_byte_pos: size as u64 - 1 }, &body[size - 1..]),
                        (Range::Inclusive { first_byte_pos: 0, last_byte_pos: 0 }, &body[0..=0]),
                        (
                            Range::Inclusive { first_byte_pos: 0, last_byte_pos: size as u64 - 1 },
                            &body[..],
                        ),
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
                        test_cases.push((size, path.as_str(), body.as_slice(), range, expected));
                    }
                }

                let mut futs = vec![];
                for (size, path, body, range, expected) in test_cases {
                    let env = &env;

                    futs.push(async move {
                        if env.supports_range() {
                            let actual = read_metadata(env, &path, range.clone()).await.unwrap();
                            assert_eq!(&actual, &expected, "size: {size} range: {range:?}");

                            let actual = read_blob(env, &path, range.clone()).await.unwrap();
                            assert_eq!(&actual, &expected, "size: {size} range: {range:?}");
                        } else {
                            let actual = read_metadata(env, &path, range.clone()).await.unwrap();
                            assert_eq!(&actual, &body[..], "size: {size} range: {range:?}");

                            let actual = read_blob(env, &path, range.clone()).await.unwrap();
                            assert_eq!(&actual, &body[..], "size: {size} range: {range:?}");
                        }
                    });
                }

                futures::future::join_all(futs).await;

                env.stop().await;
            }

            // Helper to check that fetching an invalid range returns a NotSatisfiable error.
            #[fuchsia_async::run_singlethreaded(test)]
            async fn test_fetch_range_not_satisfiable() {
                // We have a number of test cases, so we'll break them up into multiple concurrent
                // tests.

                let env = $create_env;

                let mut size_cases = vec![];
                for size in [20, $chunk_size - 1, $chunk_size, $chunk_size + 1, $chunk_size * 2 + 1]
                {
                    let path = format!("{}", size);
                    let body = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();

                    env.write_metadata(&path, &body);
                    env.write_blob(&path, &body);

                    size_cases.push((size, path));
                }

                let mut test_cases = vec![];
                for (size, path) in &size_cases {
                    let size = *size as u64;

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
                        test_cases.push((size, path.as_str(), range));
                    }
                }

                let mut futs = vec![];
                for (size, path, range) in test_cases {
                    let env = &env;

                    futs.push(async move {
                        assert_matches!(
                            read_metadata(env, &path, range.clone()).await,
                            Err(Error::RangeNotSatisfiable),
                            "size: {} range: {:?}",
                            size,
                            range
                        );

                        assert_matches!(
                            read_blob(env, &path, range.clone()).await,
                            Err(Error::RangeNotSatisfiable),
                            "size: {} range: {:?}",
                            size,
                            range
                        );
                    });
                }

                futures::future::join_all(futs).await;

                env.stop().await;
            }
        }
    };
}
pub(crate) use repo_test_suite;
