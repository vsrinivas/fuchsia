// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl;
use fidl::encoding::Decodable;
use fidl::endpoints::ServerEnd;
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_component_config::ResolvedConfig;
use fidl_fuchsia_component_decl::{ConfigChecksum, Expose, ExposeProtocol, Ref, SelfRef};
use fidl_fuchsia_io::{DirectoryMarker, DirectoryRequest};
use fidl_fuchsia_sys2 as fsys2;
use fuchsia_async;
use fuchsia_fs;
use futures::{StreamExt, TryStreamExt};
use std::collections::HashMap;
use std::sync::Arc;

/// Provides a mock `RealmExplorer` interface.
/// The `info_vec` contained in this struct will be the returned
/// result of `GetAllInstanceInfos`.
pub struct MockRealmExplorer {
    info_vec: Vec<fsys2::InstanceInfo>,
}

/// Creates the default test fixures for `MockRealmExplorer`.
impl Default for MockRealmExplorer {
    fn default() -> Self {
        let mut mock_realm_explorer = MockRealmExplorer::new();
        mock_realm_explorer.info_vec.push(fsys2::InstanceInfo {
            moniker: "./example/component".to_owned(),
            url: "".to_owned(),
            instance_id: None,
            state: fsys2::InstanceState::Resolved,
        });
        mock_realm_explorer.info_vec.push(fsys2::InstanceInfo {
            moniker: "./other/component".to_owned(),
            url: "".to_owned(),
            instance_id: None,
            state: fsys2::InstanceState::Resolved,
        });
        mock_realm_explorer.info_vec.push(fsys2::InstanceInfo {
            moniker: "./foo/component".to_owned(),
            url: "".to_owned(),
            instance_id: None,
            state: fsys2::InstanceState::Resolved,
        });
        mock_realm_explorer.info_vec.push(fsys2::InstanceInfo {
            moniker: "./foo/bar/thing".to_owned(),
            url: "".to_owned(),
            instance_id: None,
            state: fsys2::InstanceState::Resolved,
        });
        mock_realm_explorer
    }
}

impl MockRealmExplorer {
    /// Creates an empty `MockRealmExplorer`, which will return no results when called.
    pub fn new() -> Self {
        MockRealmExplorer { info_vec: vec![] }
    }

    /// Creats a `MockRealmExplorer` which will return the vector of `InstanceInfo`
    /// when called.
    pub fn new_with(info_vec: Vec<fsys2::InstanceInfo>) -> Self {
        MockRealmExplorer { info_vec }
    }

    /// Serves the `RealmExplorer` interface asynchronously and runs until the program terminates.
    pub async fn serve(self: Arc<Self>, object: ServerEnd<fsys2::RealmExplorerMarker>) {
        let (tx, rx) = futures::channel::mpsc::unbounded();
        let drain_task = fuchsia_async::Task::spawn(async move {
            rx.for_each_concurrent(None, |task| async move { task.await }).await;
        });

        let mut stream = object.into_stream().unwrap();
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                fsys2::RealmExplorerRequest::GetAllInstanceInfos { responder } => {
                    let (client_end, server_end) =
                        create_endpoints::<fsys2::InstanceInfoIteratorMarker>().unwrap();
                    let mut cloned_info = self.info_vec.clone();

                    tx.unbounded_send(fuchsia_async::Task::spawn(async move {
                        let mut first_get = true;
                        let mut stream: fsys2::InstanceInfoIteratorRequestStream =
                            server_end.into_stream().unwrap();
                        while let Some(Ok(fsys2::InstanceInfoIteratorRequest::Next { responder })) =
                            stream.next().await
                        {
                            if first_get {
                                responder.send(&mut cloned_info.iter_mut()).unwrap();
                                first_get = false;
                            } else {
                                let mut ret: Vec<fsys2::InstanceInfo> = vec![];
                                responder.send(&mut ret.iter_mut()).unwrap();
                            }
                        }
                    }))
                    .unwrap();

                    responder.send(&mut Ok(client_end)).unwrap()
                }
            }
        }
        drain_task.await;
    }

    /// Serves the `RealmExplorer` interface asynchronously and runs until the program terminates.
    /// Then, instead of needing the client to discover the protocol, return the proxy for futher
    /// test use.
    pub async fn get_proxy(self: Arc<Self>) -> fsys2::RealmExplorerProxy {
        let (proxy, server_end) = create_proxy::<fsys2::RealmExplorerMarker>().unwrap();
        fuchsia_async::Task::local(async move { self.serve(server_end).await }).detach();
        proxy
    }
}

/// Creates a encoded buffer for DirEntry.
fn dirents_files(names: &[String]) -> Vec<u8> {
    let mut bytes = vec![];
    for name in names {
        // inode: u64
        for _ in 0..8 {
            bytes.push(0);
        }
        // size: u8
        bytes.push(name.len() as u8);
        // type: u8
        bytes.push(fuchsia_fs::directory::DirentKind::File as u8);
        // name: [u8]
        for byte in name.bytes() {
            bytes.push(byte);
        }
    }
    bytes
}

/// Quick alias for for RealmQuery protocol.
type RealmQueryResult = (fsys2::InstanceInfo, Option<Box<fsys2::ResolvedState>>);

/// Builder struct for `RealmQueryResult`/
/// This is an builder interface meant to simplify building of test fixtures.
/// Example usage:
/// ```
///   MockRealmQuery.add()
///   .when("other/component") // when client queries for this string ("other/component").
///   .moniker("./other/component") // Returns the following.
///   .exposes(vec![Expose::Protocol(ExposeProtocol {
///       source: Some(Ref::Self_(SelfRef)),
///       target: Some(Ref::Self_(SelfRef)),
///       source_name: Some("src".to_owned()),
///       target_name: Some("fuchsia.io.SomeOtherThing".to_owned()),
///       ..ExposeProtocol::EMPTY
///   })])
///   .add() // Finish building the result.
///   .when("some/thing") // Start another build.
///   ...
/// ```
pub struct MockRealmQueryBuilder {
    mapping: HashMap<String, Box<MockRealmQueryBuilderInner>>,
}

/// Inner struct of `MockRealmQueryBuilder` to provide a builder interface for
/// RealmQuery protocol responses.
pub struct MockRealmQueryBuilderInner {
    when: String,
    moniker: String,
    exposes: Vec<Expose>,
    out_dir_entry: Vec<String>,
    parent: Option<Box<MockRealmQueryBuilder>>,
}

/// Convert a `MockRealmQueryBuilderInner` to a `RealmQueryResult` which we can
/// transmit over the wire.
async fn to_realm_query_result(builder: &MockRealmQueryBuilderInner) -> RealmQueryResult {
    let (exposed_client, _) = create_endpoints::<DirectoryMarker>().unwrap();
    let (ns_client, _) = create_endpoints::<DirectoryMarker>().unwrap();
    let (outdir_client, outdir_server) = create_endpoints::<DirectoryMarker>().unwrap();

    let out_dirs = builder.out_dir_entry.clone();
    fuchsia_async::Task::local(async move {
        let mut first_call = true;
        let mut stream = outdir_server.into_stream().unwrap();
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                DirectoryRequest::ReadDirents { max_bytes: _, responder } => {
                    if first_call {
                        responder.send(0, dirents_files(&out_dirs).as_slice()).unwrap();
                        first_call = false;
                    } else {
                        responder.send(0, &vec![]).unwrap();
                    }
                }
                DirectoryRequest::Rewind { responder } => {
                    responder.send(0).unwrap();
                }
                _ => panic!(),
            }
        }
    })
    .detach();

    let rs = fsys2::ResolvedState {
        uses: vec![],
        exposes: builder.exposes.clone(),
        config: Some(Box::new(ResolvedConfig {
            fields: vec![],
            checksum: ConfigChecksum::new_empty(),
        })),
        pkg_dir: None,
        execution: Some(Box::new(fsys2::ExecutionState {
            start_reason: "reason".to_owned(),
            out_dir: Some(outdir_client),
            runtime_dir: None,
        })),
        exposed_dir: exposed_client,
        ns_dir: ns_client,
    };

    (
        fsys2::InstanceInfo {
            moniker: builder.moniker.to_owned(),
            url: "".to_owned(),
            instance_id: None,
            state: fsys2::InstanceState::Resolved,
        },
        Some(Box::new(rs)),
    )
}

impl MockRealmQueryBuilderInner {
    /// Sets the result moniker.
    pub fn moniker(mut self, moniker: &str) -> Self {
        self.moniker = moniker.to_owned();
        self
    }

    /// Sets the result vector of `Expose`s.
    pub fn exposes(mut self, exposes: Vec<Expose>) -> Self {
        self.exposes = exposes;
        self
    }

    /// Add an entry to the `DirEntry` results.
    pub fn dir_entry(mut self, entry: &str) -> Self {
        self.out_dir_entry.push(entry.to_owned());
        self
    }

    /// Completes the build and returns a `MockRealmQueryBuilder`.
    pub fn add(mut self) -> MockRealmQueryBuilder {
        let mut parent = *self.parent.unwrap();
        self.parent = None;

        parent.mapping.insert(self.when.to_owned(), Box::new(self));
        parent
    }
}

impl MockRealmQueryBuilder {
    /// Create a new empty `MockRealmQueryBuilder`.
    pub fn new() -> Self {
        MockRealmQueryBuilder { mapping: HashMap::new() }
    }

    /// Start a build of `RealmQueryResult` by specifying the
    /// expected query string.
    pub fn when(self, at: &str) -> MockRealmQueryBuilderInner {
        MockRealmQueryBuilderInner {
            when: at.to_owned(),
            moniker: "".to_owned(),
            exposes: vec![],
            out_dir_entry: vec![],
            parent: Some(Box::new(self)),
        }
    }

    /// Finish the build and return servable `MockRealmQuery`.
    pub fn build(self) -> MockRealmQuery {
        MockRealmQuery { mapping: self.mapping }
    }
}

/// Provides a mock `RealmQuery` interface.
pub struct MockRealmQuery {
    /// Mapping from Moniker -> Expose.
    mapping: HashMap<String, Box<MockRealmQueryBuilderInner>>,
}

/// Creates the default test fixures for `MockRealmQuery`.
impl Default for MockRealmQuery {
    fn default() -> Self {
        MockRealmQueryBuilder::new()
            .when("example/component")
            .moniker("./example/component")
            .exposes(vec![Expose::Protocol(ExposeProtocol {
                source: Some(Ref::Self_(SelfRef)),
                target: Some(Ref::Self_(SelfRef)),
                source_name: Some("src".to_owned()),
                target_name: Some("fuchsia.diagnostics.ArchiveAccessor".to_owned()),
                ..ExposeProtocol::EMPTY
            })])
            .dir_entry("fuchsia.some.Stuff")
            .add()
            .when("other/component")
            .moniker("./other/component")
            .exposes(vec![Expose::Protocol(ExposeProtocol {
                source: Some(Ref::Self_(SelfRef)),
                target: Some(Ref::Self_(SelfRef)),
                source_name: Some("src".to_owned()),
                target_name: Some("fuchsia.io.SomeOtherThing".to_owned()),
                ..ExposeProtocol::EMPTY
            })])
            .add()
            .when("other/component")
            .moniker("./other/component")
            .exposes(vec![Expose::Protocol(ExposeProtocol {
                source: Some(Ref::Self_(SelfRef)),
                target: Some(Ref::Self_(SelfRef)),
                source_name: Some("src".to_owned()),
                target_name: Some("fuchsia.io.MagicStuff".to_owned()),
                ..ExposeProtocol::EMPTY
            })])
            .dir_entry("fuchsia.diagnostics.MagicArchiveAccessor")
            .add()
            .when("foo/component")
            .moniker("./foo/component")
            .exposes(vec![Expose::Protocol(ExposeProtocol {
                source: Some(Ref::Self_(SelfRef)),
                target: Some(Ref::Self_(SelfRef)),
                source_name: Some("src".to_owned()),
                target_name: Some("fuchsia.diagnostics.FeedbackArchiveAccessor".to_owned()),
                ..ExposeProtocol::EMPTY
            })])
            .add()
            .when("foo/bar/thing")
            .moniker("./foo/bar/thing")
            .exposes(vec![Expose::Protocol(ExposeProtocol {
                source: Some(Ref::Self_(SelfRef)),
                target: Some(Ref::Self_(SelfRef)),
                source_name: Some("src".to_owned()),
                target_name: Some("fuchsia.diagnostics.FeedbackArchiveAccessor".to_owned()),
                ..ExposeProtocol::EMPTY
            })])
            .add()
            .build()
    }
}

impl MockRealmQuery {
    /// Serves the `RealmQuery` interface asynchronously and runs until the program terminates.
    pub async fn serve(self: Arc<Self>, object: ServerEnd<fsys2::RealmQueryMarker>) {
        let mut stream = object.into_stream().unwrap();
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                fsys2::RealmQueryRequest::GetInstanceInfo { moniker, responder } => {
                    let query_moniker =
                        if moniker.starts_with("./") { &moniker[2..] } else { &moniker };
                    match self.mapping.get(query_moniker) {
                        Some(res) => {
                            responder.send(&mut Ok(to_realm_query_result(res).await)).unwrap();
                        }
                        None => {
                            responder.send(&mut Err(fsys2::RealmQueryError::BadMoniker)).unwrap()
                        }
                    }
                }
                _ => unreachable!(),
            }
        }
    }

    /// Serves the `RealmQuery` interface asynchronously and runs until the program terminates.
    /// Then, instead of needing the client to discover the protocol, return the proxy for futher
    /// test use.
    pub async fn get_proxy(self: Arc<Self>) -> fsys2::RealmQueryProxy {
        let (proxy, server_end) = create_proxy::<fsys2::RealmQueryMarker>().unwrap();
        fuchsia_async::Task::local(async move { self.serve(server_end).await }).detach();
        proxy
    }
}
