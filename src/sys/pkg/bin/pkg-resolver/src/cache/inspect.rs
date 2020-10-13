// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_inspect::{
        IntProperty, Node, NumericProperty as _, Property as _, StringProperty, UintProperty,
    },
    fuchsia_zircon as zx,
    std::marker::PhantomData,
};

fn now_monotonic_nanos() -> i64 {
    zx::Time::get_monotonic().into_nanos()
}

/// Creates Inspect wrappers for individual blob fetches.
pub struct BlobFetcher {
    node: Node,
}

impl BlobFetcher {
    /// Create a `BlobFetcher` from an Inspect node.
    pub fn from_node(node: Node) -> Self {
        Self { node }
    }

    /// Create an Inspect wrapper for an individual blob fetch.
    pub fn fetch(&self, id: &BlobId) -> Fetch {
        let node = self.node.create_child(id.to_string());
        node.record_int("fetch_ts", now_monotonic_nanos());
        Fetch { node }
    }
}

/// A blob fetch that the pkg-resolver has begun processing.
pub struct Fetch {
    node: Node,
}

impl Fetch {
    /// Mark that the blob contents will be obtained via http.
    pub fn http(self) -> FetchHttp {
        self.node.record_string("source", "http");
        FetchHttp { node: self.node }
    }

    /// Mark that the blob contents will be obtained via fuchsia.pkg/LocalMirror.
    pub fn local_mirror(self) -> FetchState<LocalMirror> {
        self.node.record_string("source", "local-mirror");
        let state = self.node.create_string("state", "initial");
        let state_ts = self.node.create_int("state_ts", now_monotonic_nanos());
        let bytes_written = self.node.create_uint("bytes_written", 0);
        let attempts = self.node.create_uint("attempts", 0);
        FetchState::<LocalMirror> {
            state,
            state_ts,
            bytes_written,
            attempts,
            _node: self.node,
            _phantom: PhantomData,
        }
    }
}

/// A blob fetch being downloaded via http.
pub struct FetchHttp {
    node: Node,
}

impl FetchHttp {
    /// Annotate the fetch with the mirror url.
    pub fn mirror(self, mirror: &str) -> FetchState<Http> {
        self.node.record_string("mirror", mirror);
        let state = self.node.create_string("state", "initial");
        let state_ts = self.node.create_int("state_ts", now_monotonic_nanos());
        let bytes_written = self.node.create_uint("bytes_written", 0);
        let attempts = self.node.create_uint("attempts", 0);
        FetchState::<Http> {
            state,
            state_ts,
            bytes_written,
            attempts,
            _node: self.node,
            _phantom: PhantomData,
        }
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
pub struct FetchState<S: State> {
    state: StringProperty,
    state_ts: IntProperty,
    bytes_written: UintProperty,
    attempts: UintProperty,
    _node: Node,
    _phantom: std::marker::PhantomData<S>,
}

impl<S: State> FetchState<S> {
    /// Increase the attempt account.
    pub fn attempt(&self) {
        self.attempts.add(1);
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

/// The terminal type of an http fetch.
pub type FetchStateHttp = FetchState<Http>;
/// The terminal type of a fuchsia.pkg/LocalMirror.
pub type FetchStateLocal = FetchState<LocalMirror>;

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_inspect_tree, testing::AnyProperty, Inspector},
    };

    const ZEROES_HASH: &'static str =
        "0000000000000000000000000000000000000000000000000000000000000000";
    const ONES_HASH: &'static str =
        "1111111111111111111111111111111111111111111111111111111111111111";

    #[test]
    fn http_state_progression() {
        let inspector = Inspector::new();

        let blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        let inspect = blob_fetcher.fetch(&BlobId::parse(ZEROES_HASH).unwrap());
        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => {
                        fetch_ts: AnyProperty,
                    }
                }
            }
        );

        let inspect = inspect.http();
        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => {
                        fetch_ts: AnyProperty,
                        source: "http",
                    }
                }
            }
        );

        let inspect = inspect.mirror("fake-mirror");
        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => {
                        fetch_ts: AnyProperty,
                        source: "http",
                        mirror: "fake-mirror",
                        state: "initial",
                        state_ts: AnyProperty,
                        bytes_written: 0u64,
                        attempts: 0u64,
                    }
                }
            }
        );

        inspect.state(Http::CreateBlob);
        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => {
                        fetch_ts: AnyProperty,
                        source: "http",
                        mirror: "fake-mirror",
                        state: "create blob",
                        state_ts: AnyProperty,
                        bytes_written: 0u64,
                        attempts: 0u64,
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
        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => {
                        fetch_ts: AnyProperty,
                    }
                }
            }
        );

        let inspect = inspect.local_mirror();
        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => {
                        fetch_ts: AnyProperty,
                        source: "local-mirror",
                        state: "initial",
                        state_ts: AnyProperty,
                        bytes_written: 0u64,
                        attempts: 0u64,
                    }
                }
            }
        );

        inspect.state(LocalMirror::CreateBlob);
        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => {
                        fetch_ts: AnyProperty,
                        source: "local-mirror",
                        state: "create blob",
                        state_ts: AnyProperty,
                        bytes_written: 0u64,
                        attempts: 0u64,
                    }
                }
            }
        );
    }

    #[test]
    fn state_does_not_change_bytes_written_or_attempts() {
        let inspector = Inspector::new();

        let blob_fetcher = BlobFetcher::from_node(inspector.root().create_child("blob_fetcher"));
        let inspect = blob_fetcher.fetch(&BlobId::parse(ZEROES_HASH).unwrap()).local_mirror();
        inspect.attempt();
        inspect.write_bytes(6);

        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => contains {
                        state: "initial",
                        bytes_written: 6u64,
                        attempts: 1u64,
                    }
                }
            }
        );

        inspect.state(LocalMirror::TruncateBlob);

        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => contains {
                        state: "truncate blob",
                        bytes_written: 6u64,
                        attempts: 1u64,
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
        inspect.write_bytes(7);

        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => contains {
                        bytes_written: 7u64,
                    }
                }
            }
        );

        inspect.write_bytes(8);

        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: contains {
                    ZEROES_HASH.to_string() => contains {
                        bytes_written: 15u64,
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

        assert_inspect_tree!(
            inspector,
            root: {
                blob_fetcher: {
                    ZEROES_HASH.to_string() => contains {},
                    ONES_HASH.to_string() => contains {},
                }
            }
        );
    }
}
