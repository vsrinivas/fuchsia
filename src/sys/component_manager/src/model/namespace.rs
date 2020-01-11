// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::PKG_PATH,
    crate::model::{
        error::ModelError, model::Model, moniker::AbsoluteMoniker, rights::Rights, routing,
    },
    cm_rust::{self, ComponentDecl, UseDecl, UseStorageDecl},
    directory_broker,
    fidl::endpoints::{create_endpoints, ClientEnd, ServerEnd},
    fidl_fuchsia_io::{self as fio, DirectoryProxy, NodeMarker},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_async::EHandle,
    fuchsia_vfs_pseudo_fs_mt::{
        directory::entry::DirectoryEntry, directory::entry_container::DirectlyMutable,
        directory::immutable::simple as pfs, execution_scope::ExecutionScope, path::Path,
    },
    fuchsia_zircon as zx,
    futures::future::{AbortHandle, Abortable, BoxFuture},
    log::*,
    std::{collections::HashMap, sync::Arc},
};

type Directory = Arc<pfs::Simple>;

pub struct IncomingNamespace {
    pub package_dir: Option<DirectoryProxy>,
    dir_abort_handles: Vec<AbortHandle>,
}

impl Drop for IncomingNamespace {
    fn drop(&mut self) {
        for abort_handle in &self.dir_abort_handles {
            abort_handle.abort();
        }
    }
}

impl IncomingNamespace {
    pub fn new(package: Option<fsys::Package>) -> Result<Self, ModelError> {
        let package_dir = match package {
            Some(package) => {
                if package.package_dir.is_none() {
                    return Err(ModelError::ComponentInvalid);
                }
                let package_dir = package
                    .package_dir
                    .unwrap()
                    .into_proxy()
                    .expect("could not convert package dir to proxy");
                Some(package_dir)
            }
            None => None,
        };
        Ok(Self { package_dir, dir_abort_handles: vec![] })
    }

    /// In addition to populating an fsys::ComponentNamespace, `populate` will start serving and install
    /// handles to pseudo directories.
    pub async fn populate<'a>(
        &'a mut self,
        model: Model,
        abs_moniker: &'a AbsoluteMoniker,
        decl: &'a ComponentDecl,
    ) -> Result<fsys::ComponentNamespace, ModelError> {
        let mut ns = fsys::ComponentNamespace { paths: vec![], directories: vec![] };

        // Populate the /pkg namespace.
        if let Some(package_dir) = self.package_dir.as_ref() {
            Self::add_pkg_directory(&mut ns, package_dir)?;
        }

        // Populate the namespace from uses, using the component manager's namespace.
        // svc_dirs will hold (path,directory) pairs. Each pair holds a path in the
        // component's namespace and a directory that ComponentMgr will host for the component.
        let mut svc_dirs = HashMap::new();

        // directory_waiters will hold Future<Output=()> objects that will wait for activity on
        // a channel and then route the channel to the appropriate component's out directory.
        let mut directory_waiters = Vec::new();

        for use_ in &decl.uses {
            match use_ {
                cm_rust::UseDecl::Directory(_) => {
                    Self::add_directory_use(
                        &mut ns,
                        &mut directory_waiters,
                        use_,
                        model.clone(),
                        abs_moniker.clone(),
                    )?;
                }
                cm_rust::UseDecl::ServiceProtocol(s) => {
                    Self::add_service_use(&mut svc_dirs, s, model.clone(), abs_moniker.clone())?;
                }
                cm_rust::UseDecl::Service(_) => {
                    return Err(ModelError::unsupported("Service capability"))
                }
                cm_rust::UseDecl::Storage(_) => {
                    Self::add_storage_use(
                        &mut ns,
                        &mut directory_waiters,
                        use_,
                        model.clone(),
                        abs_moniker.clone(),
                    )?;
                }
                cm_rust::UseDecl::Runner(_) => {
                    // Runner capabilities are handled in model::model,
                    // as these are capabilities used by the framework itself
                    // and not given to components directly.
                }
            }
        }

        // Start hosting the services directories and add them to the namespace
        self.serve_and_install_svc_dirs(&mut ns, svc_dirs)?;
        self.start_directory_waiters(directory_waiters)?;
        Ok(ns)
    }

    /// add_pkg_directory will add a handle to the component's package under /pkg in the namespace.
    fn add_pkg_directory(
        ns: &mut fsys::ComponentNamespace,
        package_dir: &DirectoryProxy,
    ) -> Result<(), ModelError> {
        let clone_dir_proxy = io_util::clone_directory(package_dir, fio::CLONE_FLAG_SAME_RIGHTS)
            .map_err(|e| ModelError::namespace_creation_failed(e))?;
        let cloned_dir = ClientEnd::new(
            clone_dir_proxy
                .into_channel()
                .expect("could not convert directory to channel")
                .into_zx_channel(),
        );
        ns.paths.push(PKG_PATH.to_str().unwrap().to_string());
        ns.directories.push(cloned_dir);
        Ok(())
    }

    /// Adds a directory waiter to `waiters` and updates `ns` to contain a handle for the
    /// directory described by `use_`. Once the channel is readable, the future calls
    /// `route_directory` to forward the channel to the source component's outgoing directory and
    /// terminates.
    fn add_directory_use(
        ns: &mut fsys::ComponentNamespace,
        waiters: &mut Vec<BoxFuture<()>>,
        use_: &UseDecl,
        model: Model,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        Self::add_directory_helper(ns, waiters, use_, model, abs_moniker)
    }

    /// Adds a directory waiter to `waiters` and updates `ns` to contain a handle for the
    /// storage described by `use_`. Once the channel is readable, the future calls
    /// `route_storage` to forward the channel to the source component's outgoing directory and
    /// terminates.
    fn add_storage_use(
        ns: &mut fsys::ComponentNamespace,
        waiters: &mut Vec<BoxFuture<()>>,
        use_: &UseDecl,
        model: Model,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        Self::add_directory_helper(ns, waiters, use_, model, abs_moniker)
    }

    fn add_directory_helper(
        ns: &mut fsys::ComponentNamespace,
        waiters: &mut Vec<BoxFuture<()>>,
        use_: &UseDecl,
        model: Model,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let target_path = match use_ {
            UseDecl::Directory(d) => d.target_path.to_string(),
            UseDecl::Storage(s) => match s {
                UseStorageDecl::Data(p) => p.to_string(),
                UseStorageDecl::Cache(p) => p.to_string(),
                UseStorageDecl::Meta => {
                    // Meta storage capabilities are handled in model::model, as these are capabilities
                    // used by the framework itself and not given to components directly.
                    return Ok(());
                }
            },
            _ => {
                panic!("not a directory or storage capability");
            }
        };
        let flags = match use_ {
            UseDecl::Directory(dir) => Rights::from(dir.rights).into_legacy(),
            UseDecl::Storage(_) => fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            _ => panic!("not a directory or storage capability"),
        };
        let use_ = use_.clone();
        let (client_end, server_end) =
            create_endpoints().expect("could not create storage proxy endpoints");
        let route_on_usage = async move {
            // Wait for the channel to become readable.
            let server_end = fasync::Channel::from_channel(server_end.into_channel())
                .expect("failed to convert server_end into async channel");
            let on_signal_fut = fasync::OnSignals::new(&server_end, zx::Signals::CHANNEL_READABLE);
            on_signal_fut.await.unwrap();

            let abs_moniker_clone = abs_moniker.clone();
            let res = async move {
                let target_realm = model.look_up_realm(&abs_moniker_clone).await?;
                routing::route_use_capability(
                    &model,
                    flags,
                    fio::MODE_TYPE_DIRECTORY,
                    String::new(),
                    &use_,
                    &target_realm,
                    server_end.into_zx_channel(),
                )
                .await
            }
            .await;
            if let Err(e) = res {
                error!("failed to route directory for component {}: {:?}", abs_moniker, e);
            }
        };

        waiters.push(Box::pin(route_on_usage));
        ns.paths.push(target_path.clone());
        ns.directories.push(client_end);
        Ok(())
    }

    /// start_directory_waiters will spawn the futures in directory_waiters as abortables, and adds
    /// the abort handles to the IncomingNamespace.
    fn start_directory_waiters(
        &mut self,
        directory_waiters: Vec<BoxFuture<'static, ()>>,
    ) -> Result<(), ModelError> {
        for waiter in directory_waiters {
            let (abort_handle, abort_registration) = AbortHandle::new_pair();
            self.dir_abort_handles.push(abort_handle);
            let future = Abortable::new(waiter, abort_registration);
            // The future for a directory waiter will only terminate once the directory channel is
            // first used, so we must start up a new task here to run the future instead of calling
            // await on it directly. This is wrapped in an async move {.await;}` block to drop
            // the unused return value.
            fasync::spawn(async move {
                let _ = future.await;
            });
        }
        Ok(())
    }

    /// Adds a service broker in `svc_dirs` for service described by `use_`. The service will be
    /// proxied to the outgoing directory of the source component.
    fn add_service_use(
        svc_dirs: &mut HashMap<String, Directory>,
        use_: &cm_rust::UseServiceProtocolDecl,
        model: Model,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let use_clone = use_.clone();
        let route_open_fn = Box::new(
            move |flags: u32,
                  mode: u32,
                  relative_path: String,
                  server_end: ServerEnd<NodeMarker>| {
                let use_ = UseDecl::ServiceProtocol(use_clone.clone());
                let model = model.clone();
                let abs_moniker = abs_moniker.clone();
                fasync::spawn(async move {
                    let abs_moniker_clone = abs_moniker.clone();
                    let res = async move {
                        let target_realm = model.look_up_realm(&abs_moniker_clone).await?;
                        routing::route_use_capability(
                            &model,
                            flags,
                            mode,
                            relative_path,
                            &use_,
                            &target_realm,
                            server_end.into_channel(),
                        )
                        .await
                    }
                    .await;
                    if let Err(e) = res {
                        error!("failed to route service for component {}: {:?}", abs_moniker, e);
                    }
                });
            },
        );

        let service_dir = svc_dirs.entry(use_.target_path.dirname.clone()).or_insert(pfs::simple());
        service_dir
            .clone()
            .add_entry(
                &use_.target_path.basename,
                directory_broker::DirectoryBroker::new(route_open_fn),
            )
            .expect("could not add service to directory");
        Ok(())
    }

    /// serve_and_install_svc_dirs will take all of the pseudo directories collected in
    /// svc_dirs (as populated by add_service_use calls), start them and install them in the
    /// namespace. The abortable handles are saved in the IncomingNamespace, to
    /// be called when the IncomingNamespace is dropped.
    fn serve_and_install_svc_dirs(
        &mut self,
        ns: &mut fsys::ComponentNamespace,
        svc_dirs: HashMap<String, Directory>,
    ) -> Result<(), ModelError> {
        for (target_dir_path, pseudo_dir) in svc_dirs {
            let (client_end, server_end) =
                create_endpoints::<NodeMarker>().expect("could not create node proxy endpoints");
            pseudo_dir.clone().open(
                ExecutionScope::from_executor(Box::new(EHandle::local())),
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                Path::empty(),
                server_end.into_channel().into(),
            );

            ns.paths.push(target_dir_path.as_str().to_string());
            let client_end = ClientEnd::new(client_end.into_channel()); // coerce to ClientEnd<Dir>
            ns.directories.push(client_end);
        }
        Ok(())
    }
}
