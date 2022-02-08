// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl::endpoints::{DiscoverableProtocolMarker as _, Proxy as _};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_logger as flogger;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::new::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
};
use fuchsia_zircon as zx;

use futures::{FutureExt as _, StreamExt as _};
use netstack_testing_common::realms::{Netstack, Netstack2};
use std::convert::TryInto as _;
use vfs::directory::entry::DirectoryEntry as _;

#[fuchsia::test]
async fn start_with_cache_no_space() {
    struct NoSpaceEntryConstructor {
        paths: Vec<String>,
    }
    struct SyncNoSpaceEntryConstructor(std::sync::Mutex<NoSpaceEntryConstructor>);

    impl vfs::directory::mutable::entry_constructor::EntryConstructor for SyncNoSpaceEntryConstructor {
        fn create_entry(
            self: std::sync::Arc<Self>,
            _parent: std::sync::Arc<dyn vfs::directory::entry::DirectoryEntry>,
            _what: vfs::directory::mutable::entry_constructor::NewEntryType,
            name: &str,
            _path: &vfs::path::Path,
        ) -> Result<std::sync::Arc<dyn vfs::directory::entry::DirectoryEntry>, zx::Status> {
            let Self(this) = &*self;
            let NoSpaceEntryConstructor { paths } = &mut *this.lock().unwrap();
            let () = paths.push(name.into());
            Err(zx::Status::NO_SPACE)
        }
    }

    let constructor = std::sync::Arc::new(SyncNoSpaceEntryConstructor(std::sync::Mutex::new(
        NoSpaceEntryConstructor { paths: Vec::new() },
    )));

    let root = vfs::mut_pseudo_directory! {};
    let (client, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .expect("failed to create FIDL endpoints");
    let () = root.open(
        vfs::execution_scope::ExecutionScope::build().entry_constructor(constructor.clone()).new(),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        0,
        vfs::path::Path::dot(),
        server.into_channel().into(),
    );

    const NETSTACK_MONIKER: &str = "netstack";
    const MOCK_CACHE_MONIKER: &str = "mock-cache";
    const CACHE_DIR_NAME: &str = "cache";
    const CACHE_DIR_PATH: &str = "/cache";
    const DIAGNOSTICS_DIR_NAME: &str = "diagnostics";
    const DIAGNOSTICS_DIR_PATH: &str = "/diagnostics";

    let builder = RealmBuilder::new().await.expect("failed to create realm builder");
    let netstack = builder
        .add_child(NETSTACK_MONIKER, Netstack2::VERSION.get_url(), ChildOptions::new())
        .await
        .expect("failed to add netstack component");
    let () = builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fnetstack::NetstackMarker>())
                .from(&netstack)
                .to(Ref::parent()),
        )
        .await
        .unwrap_or_else(|e| {
            panic!(
                "failed to expose {} from netstack: {}",
                fnetstack::NetstackMarker::PROTOCOL_NAME,
                e,
            )
        });
    let () = builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<flogger::LogSinkMarker>())
                .from(Ref::parent())
                .to(&netstack),
        )
        .await
        .unwrap_or_else(|e| {
            panic!("failed to offer {} to netstack: {}", flogger::LogSinkMarker::PROTOCOL_NAME, e)
        });
    let mock_cache = builder
        .add_local_child(
            MOCK_CACHE_MONIKER,
            move |handles: LocalComponentHandles| {
                let cache = Clone::clone(&client);
                Box::pin(async {
                    let mut fs = ServiceFs::new();
                    let () = fs.add_remote(CACHE_DIR_NAME, cache);
                    let _: &mut ServiceFs<_> =
                        fs.serve_connection(handles.outgoing_dir.into_channel())?;
                    let () = fs.collect::<()>().await;
                    Ok(())
                })
            },
            ChildOptions::new(),
        )
        .await
        .expect("failed to add mock cache component");
    let () = builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory(CACHE_DIR_NAME)
                        .path(CACHE_DIR_PATH)
                        .rights(fio::RW_STAR_DIR),
                )
                .from(&mock_cache)
                .to(&netstack),
        )
        .await
        .expect("failed to route cache to netstack");

    let mut netstack_decl = builder
        .get_component_decl(&netstack)
        .await
        .expect("failed to find netstack component decl");
    let cm_rust::ComponentDecl { exposes, capabilities, uses, .. } = &mut netstack_decl;

    // The netstack component needs to use the 'cache' directory for this test, but by default does
    // not. It's offered to netstack with the above `add_route` call using `CACHE_DIR_NAME`, but we
    // need to add in a use declaration ourselves.
    uses.push(cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
        dependency_type: cm_rust::DependencyType::Strong,
        source: cm_rust::UseSource::Parent,
        source_name: CACHE_DIR_NAME.try_into().expect("failed to convert string to path"),
        target_path: CACHE_DIR_PATH.try_into().expect("failed to convert string to path"),
        rights: fio::RW_STAR_DIR,
        subdir: None,
    }));

    // The netstack component exposes `/diagnostics` to `framework` with `connect` rights, in order
    // to serve inspect data. For this test, expose `/diagnostics` to `parent` instead, with `r*`
    // rights, so that it is accessible from the root of the realm and we can introspect it.
    let target = exposes
        .iter_mut()
        .find_map(|expose| match expose {
            cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                source_name,
                target,
                ..
            }) if source_name.str() == DIAGNOSTICS_DIR_NAME => Some(target),
            _ => None,
        })
        .expect("failed to find diagnostics expose");
    *target = cm_rust::ExposeTarget::Parent;
    let rights =
        capabilities
            .iter_mut()
            .find_map(|capability| match capability {
                cm_rust::CapabilityDecl::Directory(cm_rust::DirectoryDecl {
                    name, rights, ..
                }) if name.str() == DIAGNOSTICS_DIR_NAME => Some(rights),
                _ => None,
            })
            .expect("failed to find diagnostics capability");
    *rights = fio::R_STAR_DIR;
    let () = builder
        .replace_component_decl(&netstack, netstack_decl)
        .await
        .expect("failed to modify netstack component decl");
    let () = builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory(DIAGNOSTICS_DIR_NAME)
                        .path(DIAGNOSTICS_DIR_PATH)
                        .rights(fio::R_STAR_DIR),
                )
                .from(&netstack)
                .to(Ref::parent()),
        )
        .await
        .expect("failed to expose diagnostics directory from netstack");
    let realm_instance = builder.build().await.expect("error creating realm");

    let netstack = realm_instance
        .root
        .connect_to_protocol_at_exposed_dir::<fnetstack::NetstackMarker>()
        .expect("failed to connect to netstack");
    let mut netstack_fut = netstack.on_closed().fuse();

    let (client_channel, server_channel) =
        zx::Channel::create().expect("failed to create zircon channel");
    let () = realm_instance
        .root
        .connect_request_to_named_protocol_at_exposed_dir(DIAGNOSTICS_DIR_NAME, server_channel)
        .expect("failed to open diagnostics directory exposed from netstack");
    let client_channel = fuchsia_async::Channel::from_channel(client_channel)
        .expect("failed to create async channel");
    let proxy = fidl_fuchsia_io::DirectoryProxy::new(client_channel);

    let () = futures::select! {
        res = netstack_fut => panic!("netstack unexpectedly exited; got signals: {:?}", res),
        res = proxy.describe() => {
            let info = res.expect("failed to describe diagnostics directory");
            assert_eq!(
                info,
                fidl_fuchsia_io::NodeInfo::Directory(fidl_fuchsia_io::DirectoryObject)
            );
        }
    };

    let SyncNoSpaceEntryConstructor(this) = &*constructor;
    let NoSpaceEntryConstructor { paths } = &*this.lock().unwrap();
    assert_eq!(paths[..], ["pprof"]);
}
