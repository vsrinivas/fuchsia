// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    cache_manager_config_lib::Config,
    fidl,
    fidl::endpoints::Proxy,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    std::process,
    tracing::*,
};

#[fuchsia::main(logging_tags=["cache_manager"])]
async fn main() -> Result<(), Error> {
    info!("cache manager started");
    let config = Config::take_from_startup_handle();
    if config.cache_clearing_threshold > 100 {
        error!(
            "cache clearing threshold is too high, must be <= 100 but it is {}",
            config.cache_clearing_threshold
        );
        process::exit(1);
    }
    info!(
        "cache will be cleared when storage passes {}% capacity",
        config.cache_clearing_threshold
    );
    info!("checking storage every {} milliseconds", config.storage_checking_frequency);

    let storage_admin = fclient::connect_to_protocol::<fsys::StorageAdminMarker>()
        .context("failed opening storage admin")?;

    // Sleep for the check interval, then see if we're over the clearing threshold.
    // If we are over the threshold, clear the cache. This panics if we lose the
    // connect to the StorageAdmineProtocol.
    loop {
        fasync::Timer::new(std::time::Duration::from_millis(config.storage_checking_frequency))
            .await;
        let storage_state = {
            match get_storage_utilization(&storage_admin).await {
                Ok(utilization) => {
                    if utilization.percent_used() > 100 {
                        warn!("storage utlization is above 100%, clearing storage.");
                    }
                    utilization
                }
                Err(e) => match e.downcast_ref::<fidl::Error>() {
                    Some(fidl::Error::ClientChannelClosed { .. }) => {
                        panic!(
                            "cache manager's storage admin channel closed unexpectedly, \
                            is component manager dead?"
                        );
                    }
                    _ => {
                        error!("failed getting cache utilization, will try again later: {:?}", e);
                        continue;
                    }
                },
            }
        };

        // Not enough storage is used, sleep and wait for changes
        if storage_state.percent_used() < config.cache_clearing_threshold {
            continue;
        }

        // Clear the cache
        info!("storage utilization is at {}%, which is above our threshold of {}%, beginning to clear cache storage", storage_state.percent_used(), config.cache_clearing_threshold);

        match clear_cache_storage(&storage_admin).await {
            Err(e) => match e.downcast_ref::<fidl::Error>() {
                Some(fidl::Error::ClientChannelClosed { .. }) => {
                    panic!(
                        "cache manager's storage admin channel closed while clearing storage \
                        is component manager dead?"
                    );
                }
                _ => {
                    error!("non-fatal error while clearing cache: {:?}", e);
                    continue;
                }
            },
            _ => {}
        }

        let storage_state_after = match get_storage_utilization(&storage_admin).await {
            Err(e) => match e.downcast_ref::<fidl::Error>() {
                Some(fidl::Error::ClientChannelClosed { .. }) => {
                    panic!(
                        "cache manager's storage admin channel closed while checking utlization \
                            after cache clearing, is component manager dead?"
                    );
                }
                _ => {
                    error!("non-fatal getting storage utlization {:?}", e);
                    continue;
                }
            },
            Ok(u) => u,
        };

        if storage_state.percent_used() > config.cache_clearing_threshold {
            warn!("storage usage still exceeds threshold after cache clearing, used_bytes={} total_bytes={}", storage_state.used_bytes, storage_state.total_bytes);
        }

        if storage_state_after.percent_used() >= storage_state.percent_used() {
            // TODO should we panic or just log an error?
            warn!("cache manager did not reduce storage pressure, maybe something is consuming storage faster than it can be cleared?");
        }
    }
}

#[derive(PartialEq, Debug)]
struct StorageState {
    total_bytes: u64,
    used_bytes: u64,
}

impl StorageState {
    fn percent_used(&self) -> u64 {
        if self.total_bytes > 0 {
            self.used_bytes * 100 / self.total_bytes
        } else {
            0
        }
    }
}

async fn query_filesystem_use(cache_proxy: &fio::DirectoryProxy) -> Result<StorageState, Error> {
    let (_, filesystem_info) =
        cache_proxy.query_filesystem().await.context("failed to query filesystem")?;
    let filesystem_info =
        filesystem_info.ok_or(format_err!("filesystem_info not supplied by filesystem"))?;

    // The number of bytes which may be allocated plus the number of bytes which have been
    // allocated. |total_bytes| is the amount of data (not counting metadata like inode storage)
    // that minfs has currently allocated from the volume manager, while used_bytes is the amount
    // of those actually used for current storage.
    let total_bytes = filesystem_info.free_shared_pool_bytes + filesystem_info.total_bytes;
    if total_bytes == 0 {
        return Err(format_err!("unable to determine storage pressure"));
    }
    if total_bytes < filesystem_info.used_bytes {
        return Err(format_err!("filesystem appears to be using more bytes than exist"));
    }

    let storage_state = StorageState { total_bytes, used_bytes: filesystem_info.used_bytes };
    Ok(storage_state)
}

/// Check the current cache storage utilization. If no components are using cache, the utilization
/// reported by the filesystem is not checked and utlization is reported as zero.
async fn get_storage_utilization(
    storage_admin: &fsys::StorageAdminProxy,
) -> Result<StorageState, Error> {
    let (realm_contents, realm_storage_contents) =
        fidl::endpoints::create_proxy::<fsys::StorageIteratorMarker>()?;
    let target_moniker = {
        storage_admin.list_storage_in_realm("./", realm_storage_contents).await?.map_err(
            |protocol_error| {
                format_err!("protocol error listing realm storage: {:?}", protocol_error)
            },
        )?;
        let monikers = realm_contents
            .next()
            .await
            .map_err(|e| format_err!("storage iteration failed: {:?}", e))?;

        // If nothing is using storage, assume utilization is zero, if it isn't
        // we have no way to check anyway.
        if monikers.len() == 0 {
            return Ok(StorageState { total_bytes: 0, used_bytes: 0 });
        } else {
            monikers[0].clone()
        }
    };

    let (client, server) = fidl::endpoints::create_proxy::<fio::NodeMarker>()?;
    storage_admin.open_component_storage(
        &target_moniker,
        fio::OpenFlags::RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        server,
    )?;

    let client_dir = fio::DirectoryProxy::from_channel(client.into_channel().unwrap());
    query_filesystem_use(&client_dir).await
}

/// Tries to delete the cache for all components. Failures for individual components are ignored,
/// but if `storage_admin` is closed that is reported as the `fidl::Error::ClientChannelClosed`
/// that it is.
async fn clear_cache_storage(storage_admin: &fsys::StorageAdminProxy) -> Result<(), Error> {
    let (realm_contents, realm_storage_contents) =
        fidl::endpoints::create_proxy::<fsys::StorageIteratorMarker>()?;

    storage_admin
        .list_storage_in_realm("./", realm_storage_contents)
        .await?
        .map_err(|e| format_err!("failed to get storage iterator: {:?}", e))?;

    loop {
        let monikers = realm_contents
            .next()
            .await
            .map_err(|e| format_err!("storage iteration failed: {:?}", e))?;
        if monikers.len() == 0 {
            break;
        }

        for m in monikers {
            // Try to clear storage for each moniker. Errors related to
            // individual components are logged and we continue down the list.
            // A closed channel is fatal and we return immediately.
            match storage_admin.delete_component_storage(&m).await {
                Ok(Ok(())) => {
                    // great, it worked!
                }
                Err(fidl::Error::ClientChannelClosed { status, protocol_name }) => {
                    // a closed channel is fatal
                    return Err(fidl::Error::ClientChannelClosed { status, protocol_name }.into());
                }
                Err(e) => {
                    warn!("fidl error clearing storage for component {}: {:?}", m, e);
                }
                Ok(Err(e)) => {
                    warn!("protocol error clearing storage for {}: {:?}", m, e);
                }
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {

    use {
        crate::{clear_cache_storage, get_storage_utilization, StorageState},
        async_trait::async_trait,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{channel::mpsc, future::Either, StreamExt, TryStreamExt},
        std::{
            boxed::Box, collections::HashMap, fmt::Formatter, future::Future, pin::Pin, sync::Arc,
        },
        vfs::{
            directory::{
                connection::io1::DerivedConnection,
                dirents_sink,
                entry::{DirectoryEntry, EntryInfo},
                entry_container::{Directory, DirectoryWatcher},
                immutable::connection::io1::ImmutableConnection,
                traversal_position::TraversalPosition,
            },
            execution_scope::ExecutionScope,
            path::Path,
        },
    };

    struct FakeDir {
        used: u32,
        total: u32,
        scope: ExecutionScope,
    }

    impl std::fmt::Debug for FakeDir {
        fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
            f.write_fmt(format_args!("used: {}; total: {}", self.used, self.total))
        }
    }
    impl DirectoryEntry for FakeDir {
        fn open(
            self: Arc<Self>,
            _scope: ExecutionScope,
            flags: fio::OpenFlags,
            _mode: u32,
            _path: Path,
            server_end: ServerEnd<fio::NodeMarker>,
        ) {
            <ImmutableConnection as DerivedConnection>::create_connection(
                self.scope.clone(),
                self.clone(),
                flags,
                server_end,
            );
        }

        fn entry_info(&self) -> EntryInfo {
            panic!("not implemented!");
        }
    }

    #[async_trait]
    impl Directory for FakeDir {
        async fn read_dirents<'a>(
            &'a self,
            _pos: &'a TraversalPosition,
            _sink: Box<dyn dirents_sink::Sink>,
        ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), zx::Status> {
            Err(zx::Status::INTERNAL)
        }

        fn register_watcher(
            self: Arc<Self>,
            _scope: ExecutionScope,
            _mask: fio::WatchMask,
            _watcher: DirectoryWatcher,
        ) -> Result<(), zx::Status> {
            Err(zx::Status::INTERNAL)
        }

        fn unregister_watcher(self: Arc<Self>, _key: usize) {
            panic!("not implemented!");
        }

        fn close(&self) -> Result<(), zx::Status> {
            Err(zx::Status::INTERNAL)
        }

        fn query_filesystem(&self) -> Result<fio::FilesystemInfo, zx::Status> {
            Ok(fio::FilesystemInfo {
                total_bytes: self.total.into(),
                used_bytes: self.used.into(),
                total_nodes: 0,
                used_nodes: 0,
                free_shared_pool_bytes: 0,
                fs_id: 0,
                block_size: 512,
                max_filename_size: 100,
                fs_type: 0,
                padding: 0,
                name: [0; 32],
            })
        }
        async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
            Err(zx::Status::INTERNAL)
        }
    }

    struct ComponentStorage {
        fail_to_delete: bool,
        dir: Option<Arc<FakeDir>>,
    }

    struct FakeStorageServer {
        monikers: HashMap<String, ComponentStorage>,
        chan: ServerEnd<fsys::StorageAdminMarker>,
    }

    impl FakeStorageServer {
        /// Run the fake server. The server sends monikers it receives it storage deletion requests
        /// over the `moniker_channel`.
        pub fn run_server(
            self,
            moniker_channel: mpsc::UnboundedSender<String>,
        ) -> Pin<Box<impl Future<Output = ()>>> {
            let server = async move {
                let mut req_stream = self.chan.into_stream().unwrap();
                while let Ok(request) = req_stream.try_next().await {
                    match request {
                        Some(fsys::StorageAdminRequest::DeleteComponentStorage {
                            relative_moniker,
                            responder,
                        }) => match self.monikers.get(&relative_moniker) {
                            Some(ComponentStorage { fail_to_delete: true, dir: None }) => {
                                responder.send(&mut Err(fcomponent::Error::AccessDenied)).unwrap();
                                moniker_channel.unbounded_send(relative_moniker.clone()).unwrap();
                            }
                            None => {
                                let _ =
                                    responder.send(&mut Err(fcomponent::Error::ResourceNotFound));
                                panic!(
                                    "'{}' not found or found more than once in monikers\nPossibly \
                                    storage was cleared for this moniker more than once?",
                                    relative_moniker
                                );
                            }
                            _ => {
                                let _ = responder.send(&mut Ok(()));
                                moniker_channel.unbounded_send(relative_moniker.clone()).unwrap();
                            }
                        },
                        Some(fsys::StorageAdminRequest::ListStorageInRealm {
                            relative_moniker,
                            iterator,
                            responder,
                        }) => {
                            assert_eq!("./".to_string(), relative_moniker);
                            let _ = responder.send(&mut Ok(()));

                            let monikers = self
                                .monikers
                                .keys()
                                .map(|m| m.clone())
                                .collect::<Vec<String>>()
                                .clone();

                            // Spawn a handler to send things over the storage iterator channel.
                            // We can't manage it in the same invocation because the requester can
                            // call back to this server before finishing with the storage iterator
                            // channel.
                            fasync::Task::local(async move {
                                Self::handle_iterator_request(monikers, iterator).await;
                            })
                            .detach();
                        }
                        Some(fsys::StorageAdminRequest::OpenComponentStorage {
                            relative_moniker,
                            object,
                            flags,
                            mode,
                            control_handle: _,
                        }) => match self.monikers.get(&relative_moniker) {
                            Some(ComponentStorage {
                                fail_to_delete: _,
                                dir: Some(component_dir),
                            }) => {
                                component_dir.clone().open(
                                    component_dir.scope.clone(),
                                    flags,
                                    mode,
                                    vfs::path::Path::dot(),
                                    object,
                                );
                            }
                            _ => panic!("unexpected request"),
                        },
                        None => return,
                        _ => panic!("operation unexpected and not supported"),
                    }
                }
            };
            Box::pin(server)
        }

        // Given a storage iterator channel and a vector of strings, send the strings over the
        // channel in one batch. Then send an empty batch, which, per the protocol, indicates
        // "no more monikers".
        async fn handle_iterator_request(
            monikers: Vec<String>,
            iterator: ServerEnd<fsys::StorageIteratorMarker>,
        ) {
            let mut iterator_stream = iterator.into_stream().unwrap();
            let mut once = false;
            while let Ok(iterator_request) = iterator_stream.try_next().await {
                match iterator_request {
                    Some(fsys::StorageIteratorRequest::Next { responder }) => {
                        if once {
                            let empty_vec: Vec<String> = Vec::new();
                            let mut ref_to_empty = empty_vec.iter().map(|s| s.as_str());
                            responder.send(&mut ref_to_empty).unwrap();
                            break;
                        }
                        let mut ref_vec = monikers.iter().map(|s| s.as_str());
                        responder.send(&mut ref_vec).unwrap();
                    }
                    None => break,
                }
                once = true;
            }
        }
    }

    /// Test the `clear_cache_storage` function. We expect that when called, `clear_cache_storage`
    /// talks to the StorageAdmin protocol and gets all the storage monikers. Then we expect the
    /// function to ask the StorageAdmin protocol to clear the cache for each of the monikers.
    #[fuchsia::test]
    async fn test_clear_cache_storage() {
        let mut monikers = vec!["a".to_string(), "b".to_string(), "c/d".to_string()];
        let (client, server) =
            fidl::endpoints::create_endpoints::<fsys::StorageAdminMarker>().unwrap();

        let (deleted_monikers_tx, deleted_monikers_rx) =
            futures::channel::mpsc::unbounded::<String>();

        let mut components_map = HashMap::<String, ComponentStorage>::new();
        for m in &monikers {
            components_map.insert(m.clone(), ComponentStorage { fail_to_delete: false, dir: None });
        }
        let fake_store_admin = FakeStorageServer { monikers: components_map, chan: server };
        let store_admin_fut = fake_store_admin.run_server(deleted_monikers_tx);

        // Run the clear_cache_storage function against the channel held by the fake server
        let test = Box::pin(async move {
            let storage_admin = client.into_proxy().unwrap();
            clear_cache_storage(&storage_admin).await.unwrap();
        });

        // Run both the fake server and the test function. We expect the function under test
        // to complete first. We don't check that the fake server completes and simply drop it.
        match futures::future::select(test, store_admin_fut).await {
            Either::Left(((), _fake_server)) => (),
            Either::Right(((), _test)) => panic!("fake server exited unexpectedly!"),
        }

        for m in deleted_monikers_rx.collect::<Vec<String>>().await.into_iter() {
            if !monikers.contains(&m) {
                panic!("Moniker {} requested, but was never supplied to client", m);
            }
            monikers.retain(|moniker| *moniker != m);
        }

        // Check that we saw deletion requests for all monikers.
        if monikers.len() != 0 {
            panic!("Deletion requests not received for monikers: {:?}", monikers);
        }
    }

    #[fuchsia::test]
    async fn test_clear_cache_storage_error_return_with_closed_channel() {
        let (client, server) =
            fidl::endpoints::create_endpoints::<fsys::StorageAdminMarker>().unwrap();
        drop(server);

        match clear_cache_storage(&client.into_proxy().unwrap()).await {
            Err(e) => match e.downcast_ref::<fidl::Error>() {
                Some(fidl::Error::ClientChannelClosed { .. }) => {}
                other => panic!("unexpected error from closed channel: {:?}", other),
            },
            _ => {
                panic!("storage cleaning should have returned an error when using a closed channel")
            }
        }
    }

    #[fuchsia::test]
    async fn test_get_storage_utlization_error_return_with_closed_channel() {
        let (client, server) =
            fidl::endpoints::create_endpoints::<fsys::StorageAdminMarker>().unwrap();
        drop(server);

        match get_storage_utilization(&client.into_proxy().unwrap()).await {
            Err(e) => match e.downcast_ref() {
                Some(fidl::Error::ClientChannelClosed { .. }) => {}
                other => panic!("unexpected error from closed channel: {:?}", other),
            },
            _ => {
                panic!("storage cleaning should have returned an error when using a closed channel")
            }
        }
    }

    /// Test the `clear_cache_storage` function works properly when the StorageAdmin throws
    /// errors during storage deletion.
    #[fuchsia::test]
    async fn test_clear_cache_storage_with_error() {
        let mut monikers = vec!["a".to_string(), "b".to_string(), "c/d".to_string()];
        let (client, server) =
            fidl::endpoints::create_endpoints::<fsys::StorageAdminMarker>().unwrap();

        let (deleted_monikers_tx, deleted_monikers_rx) =
            futures::channel::mpsc::unbounded::<String>();

        let mut components_map = HashMap::<String, ComponentStorage>::new();
        for m in &monikers {
            components_map.insert(m.clone(), ComponentStorage { fail_to_delete: false, dir: None });
        }

        // add two monikers that will generate an error, the server should still see them
        // we add two because we want to validate that after an error the test function
        // continues to run
        monikers.push("err1".to_string());
        components_map
            .insert("err1".to_string(), ComponentStorage { fail_to_delete: true, dir: None });

        monikers.push("err2".to_string());
        components_map
            .insert("err2".to_string(), ComponentStorage { fail_to_delete: true, dir: None });

        let fake_store_admin = FakeStorageServer { monikers: components_map, chan: server };
        let store_admin_fut = fake_store_admin.run_server(deleted_monikers_tx);

        // Run the clear_cache_storage function against the channel held by the fake server
        let test = Box::pin(async move {
            let storage_admin = client.into_proxy().unwrap();
            clear_cache_storage(&storage_admin).await.unwrap();
        });

        // Run both the fake server and the test function. We expect the function under test
        // to complete first. We don't check that the fake server completes and simply drop it.
        match futures::future::select(test, store_admin_fut).await {
            Either::Left(((), _fake_server)) => (),
            Either::Right(((), _test)) => panic!("fake server exited unexpectedly!"),
        }

        for m in deleted_monikers_rx.collect::<Vec<String>>().await.into_iter() {
            if !monikers.contains(&m) {
                panic!("Moniker {} requested, but was never supplied to client", m);
            }
            monikers.retain(|moniker| *moniker != m);
        }

        // Check that we saw deletion requests for all monikers.
        if monikers.len() != 0 {
            panic!("Deletion requests not received for monikers: {:?}", monikers);
        }
    }

    #[fuchsia::test]
    async fn test_get_storage_utlization() {
        let execution_scope = ExecutionScope::new();
        let monikers = vec!["a".to_string(), "b".to_string(), "c/d".to_string()];
        let (client, server) =
            fidl::endpoints::create_endpoints::<fsys::StorageAdminMarker>().unwrap();

        let fake_dir = Arc::new(FakeDir { used: 10, total: 1000, scope: execution_scope.clone() });
        let storage_state =
            StorageState { total_bytes: fake_dir.total.into(), used_bytes: fake_dir.used.into() };

        let mut component_storage: HashMap<String, ComponentStorage> = HashMap::new();
        for m in monikers {
            component_storage.insert(
                m.clone(),
                ComponentStorage { fail_to_delete: false, dir: Some(fake_dir.clone()) },
            );
        }

        let (deleted_monikers_tx, _deleted_monikers_rx) =
            futures::channel::mpsc::unbounded::<String>();
        let fake_storage_admin = FakeStorageServer { monikers: component_storage, chan: server };
        let server = fake_storage_admin.run_server(deleted_monikers_tx);

        let test_fn = Box::pin(async move {
            let storage_admin = client.into_proxy().unwrap();
            get_storage_utilization(&storage_admin).await
        });

        match futures::future::select(server, test_fn).await {
            Either::Left(((), _test_fn)) => panic!("fake server exited unexpectedly!"),
            Either::Right((utilization, _fake_server)) => {
                assert_eq!(utilization.unwrap().percent_used(), storage_state.percent_used());
            }
        }
    }
}
