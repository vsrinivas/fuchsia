// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::cache::BlobFetchParams,
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_inspect::{
        IntProperty, Node, NumericProperty as _, Property as _, StringProperty, UintProperty,
    },
    fuchsia_zircon as zx,
    std::{
        marker::PhantomData,
        sync::atomic::{AtomicU32, Ordering},
    },
};

fn now_monotonic_nanos() -> i64 {
    zx::Time::get_monotonic().into_nanos()
}

/// Creates Inspect wrappers for individual blob fetches.
pub struct BlobFetcher {
    queue: Node,
    _node: Node,
}

impl BlobFetcher {
    /// Create a `BlobFetcher` from an Inspect node.
    pub fn from_node_and_params(node: Node, params: &BlobFetchParams) -> Self {
        node.record_uint("blob_header_timeout_seconds", params.header_network_timeout().as_secs());
        node.record_uint("blob_body_timeout_seconds", params.body_network_timeout().as_secs());
        node.record_uint(
            "blob_download_resumption_attempts_limit",
            params.download_resumption_attempts_limit(),
        );
        Self { queue: node.create_child("queue"), _node: node }
    }

    /// Create an Inspect wrapper for an individual blob fetch.
    pub fn fetch(&self, id: &BlobId) -> NeedsRemoteType {
        let node = self.queue.create_child(id.to_string());
        node.record_int("fetch_ts", now_monotonic_nanos());
        NeedsRemoteType { node }
    }
}

/// A blob fetch that the pkg-resolver has begun processing.
pub struct NeedsRemoteType {
    node: Node,
}

impl NeedsRemoteType {
    /// Mark that the blob contents will be obtained via http.
    pub fn http(self) -> NeedsMirror {
        self.node.record_string("source", "http");
        NeedsMirror { node: self.node }
    }

    /// Mark that the blob contents will be obtained via fuchsia.pkg/LocalMirror.
    pub fn local_mirror(self) -> TriggerAttempt<LocalMirror> {
        self.node.record_string("source", "local-mirror");
        TriggerAttempt::<LocalMirror>::new(self.node)
    }
}

/// A blob fetch being downloaded via http.
pub struct NeedsMirror {
    node: Node,
}

impl NeedsMirror {
    /// Annotate the fetch with the mirror url.
    pub fn mirror(self, mirror: &str) -> TriggerAttempt<Http> {
        self.node.record_string("mirror", mirror);
        TriggerAttempt::<Http>::new(self.node)
    }
}

pub struct TriggerAttempt<S: State> {
    attempt_count: AtomicU32,
    attempts: Node,
    _node: Node,
    _phantom: std::marker::PhantomData<S>,
}

impl<S: State> TriggerAttempt<S> {
    fn new(node: Node) -> Self {
        Self {
            attempt_count: AtomicU32::new(0),
            attempts: node.create_child("attempts"),
            _node: node,
            _phantom: PhantomData,
        }
    }

    pub fn attempt(&self) -> Attempt<S> {
        // Don't zero-index attempts so it is obvious in inspect that multiple attempts
        // have occurred.
        let index = 1 + self.attempt_count.fetch_add(1, Ordering::SeqCst);
        let node = self.attempts.create_child(index.to_string());
        let state = node.create_string("state", "initial");
        let state_ts = node.create_int("state_ts", now_monotonic_nanos());
        let bytes_written = node.create_uint("bytes_written", 0);
        Attempt::<S> { state, state_ts, bytes_written, node, _phantom: PhantomData }
    }
}

/// Sub-states for an http fetch.
pub enum Http {
    CreateBlob,
    DownloadBlob,
    CloseBlob,
    HttpGet,
    TruncateBlob,
    ReadHttpBody,
    WriteBlob,
    WriteComplete,
}

/// Sub-states for a fuchsia.pkg/LocalMirror fetch.
pub enum LocalMirror {
    CreateBlob,
    GetBlob,
    TruncateBlob,
    ReadBlob,
    WriteBlob,
    CloseBlob,
}

/// A sub-state for a fetch. The stringification will be exported via Inspect.
pub trait State {
    fn as_str(&self) -> &'static str;
}

impl State for Http {
    fn as_str(&self) -> &'static str {
        match self {
            Http::CreateBlob => "create blob",
            Http::DownloadBlob => "download blob",
            Http::CloseBlob => "close blob",
            Http::HttpGet => "http get",
            Http::TruncateBlob => "truncate blob",
            Http::ReadHttpBody => "read http body",
            Http::WriteBlob => "write blob",
            Http::WriteComplete => "write complete",
        }
    }
}

impl State for LocalMirror {
    fn as_str(&self) -> &'static str {
        match self {
            LocalMirror::CreateBlob => "create blob",
            LocalMirror::GetBlob => "get blob",
            LocalMirror::TruncateBlob => "truncate blob",
            LocalMirror::ReadBlob => "read blob",
            LocalMirror::WriteBlob => "write blob",
            LocalMirror::CloseBlob => "close blob",
        }
    }
}

/// The terminal type of the fetch Inspect wrappers. This ends the use of move semantics to enforce
/// type transitions because at this point in cache.rs the type is being passed into and out of
/// functions and captured by FnMut.
pub struct Attempt<S: State> {
    state: StringProperty,
    state_ts: IntProperty,
    bytes_written: UintProperty,
    node: Node,
    _phantom: std::marker::PhantomData<S>,
}

impl<S: State> Attempt<S> {
    /// Set the expected size in bytes of the blob.
    pub fn expected_size_bytes(&self, size: u64) -> &Self {
        self.node.record_uint("expected_size_bytes", size);
        self
    }

    /// Mark that `bytes` more bytes of the blob have been written to blobfs.
    pub fn write_bytes(&self, bytes: usize) -> &Self {
        self.bytes_written.add(bytes as u64);
        self
    }

    /// Change the sub-state of this fetch.
    pub fn state(&self, state: S) {
        self.state.set(state.as_str());
        self.state_ts.set(now_monotonic_nanos());
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_data_tree, testing::AnyProperty, Inspector},
        std::time::Duration,
    };

    const ZEROES_HASH: &str = "0000000000000000000000000000000000000000000000000000000000000000";
    const ONES_HASH: &str = "1111111111111111111111111111111111111111111111111111111111111111";

    impl BlobFetcher {
        fn from_node(node: Node) -> Self {
            Self::from_node_and_params(
                node,
                &BlobFetchParams::builder()
                    .header_network_timeout(Duration::from_secs(0))
                    .body_network_timeout(Duration::from_secs(1))
                    .download_resumption_attempts_limit(2),
            )
        }
    }

    #[test]
    fn initial_state() {
        let inspector = Inspector::new();

        let _blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    blob_header_timeout_seconds: 0u64,
                    blob_body_timeout_seconds: 1u64,
                    blob_download_resumption_attempts_limit: 2u64,
                    queue: {}
                }
            }
        );
    }

    #[test]
    fn http_state_progression() {
        let inspector = Inspector::new();

        let blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        let inspect = blob_fetcher.fetch(&BlobId::parse(ZEROES_HASH).unwrap());
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                        }
                    }
                }
            }
        );

        let inspect = inspect.http();
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                            source: "http",
                        }
                    }
                }
            }
        );

        let inspect = inspect.mirror("fake-mirror");
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                            source: "http",
                            mirror: "fake-mirror",
                            attempts: {},
                        }
                    }
                }
            }
        );

        let attempt = inspect.attempt();
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                            source: "http",
                            mirror: "fake-mirror",
                            attempts: {
                                "1": {
                                    state: "initial",
                                    state_ts: AnyProperty,
                                    bytes_written: 0u64,
                                }
                            },
                        }
                    }
                }
            }
        );

        attempt.state(Http::CreateBlob);
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                            source: "http",
                            mirror: "fake-mirror",
                            attempts: {
                                "1": {
                                    state: "create blob",
                                    state_ts: AnyProperty,
                                    bytes_written: 0u64,
                                }
                            },
                        }
                    }
                }
            }
        );

        drop(attempt);
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                            source: "http",
                            mirror: "fake-mirror",
                            attempts: {},
                        }
                    }
                }
            }
        );
    }

    #[test]
    fn local_mirror_state_progression() {
        let inspector = Inspector::new();

        let blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        let inspect = blob_fetcher.fetch(&BlobId::parse(ZEROES_HASH).unwrap());
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                        }
                    }
                }
            }
        );

        let inspect = inspect.local_mirror();
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                            source: "local-mirror",
                            attempts: {},
                        }
                    }
                }
            }
        );

        let attempt = inspect.attempt();
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                            source: "local-mirror",
                            attempts: {
                                "1": {
                                    state: "initial",
                                    state_ts: AnyProperty,
                                    bytes_written: 0u64,
                                }
                            },
                        }
                    }
                }
            }
        );

        attempt.state(LocalMirror::CreateBlob);
        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => {
                            fetch_ts: AnyProperty,
                            source: "local-mirror",
                            attempts: {
                                "1": {
                                    state: "create blob",
                                    state_ts: AnyProperty,
                                    bytes_written: 0u64,
                                }
                            },
                        }
                    }
                }
            }
        );
    }

    #[test]
    fn state_does_not_change_other_data() {
        let inspector = Inspector::new();

        let blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        let inspect = blob_fetcher.fetch(&BlobId::parse(ZEROES_HASH).unwrap()).local_mirror();
        let attempt = inspect.attempt();
        attempt.expected_size_bytes(9);
        attempt.write_bytes(6);

        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => contains {
                            attempts: {
                                "1": {
                                    state: "initial",
                                    state_ts: AnyProperty,
                                    expected_size_bytes: 9u64,
                                    bytes_written: 6u64,
                                }
                            }
                        }
                    }
                }
            }
        );

        attempt.state(LocalMirror::TruncateBlob);

        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => contains {
                            attempts: {
                                "1": {
                                    state: "truncate blob",
                                    state_ts: AnyProperty,
                                    expected_size_bytes: 9u64,
                                    bytes_written: 6u64,
                                }
                            }
                        }
                    }
                }
            }
        );
    }

    #[test]
    fn write_bytes_is_cumulative() {
        let inspector = Inspector::new();

        let blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        let inspect = blob_fetcher.fetch(&BlobId::parse(ZEROES_HASH).unwrap()).local_mirror();
        let attempt = inspect.attempt();
        attempt.write_bytes(7);

        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => contains {
                            attempts: {
                                "1": contains {
                                    bytes_written: 7u64,
                                }
                            }
                        }
                    }
                }
            }
        );

        attempt.write_bytes(8);

        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => contains {
                            attempts: {
                                "1": contains {
                                    bytes_written: 15u64,
                                }
                            }
                        }
                    }
                }
            }
        );
    }

    #[test]
    fn multiple_fetches() {
        let inspector = Inspector::new();

        let blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        let _inspect0 = blob_fetcher.fetch(&BlobId::parse(ZEROES_HASH).unwrap());
        let _inspect1 = blob_fetcher.fetch(&BlobId::parse(ONES_HASH).unwrap());

        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => contains {},
                        ONES_HASH.to_string() => contains {},
                    }
                }
            }
        );
    }

    #[test]
    fn multiple_attempts() {
        let inspector = Inspector::new();

        let blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        let inspect = blob_fetcher.fetch(&BlobId::parse(ZEROES_HASH).unwrap()).local_mirror();
        let _attempt0 = inspect.attempt();
        let _attempt1 = inspect.attempt();

        assert_data_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    queue: {
                        ZEROES_HASH.to_string() => contains {
                            attempts: {
                                "1": contains {},
                                "2": contains {},
                            }
                        }
                    }
                }
            }
        );
    }
}
