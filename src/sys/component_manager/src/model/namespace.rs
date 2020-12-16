// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::ComponentCapability,
        constants::PKG_PATH,
        model::{
            error::ModelError,
            logging::{FmtArgsLogger, LOGGER as MODEL_LOGGER},
            realm::{Package, Runtime, WeakRealm},
            rights::Rights,
            routing,
        },
    },
    anyhow::{Context, Error},
    cm_rust::{self, ComponentDecl, UseDecl, UseProtocolDecl},
    directory_broker,
    fidl::endpoints::{create_endpoints, ClientEnd, Proxy, ServerEnd, ServiceMarker},
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::{self as fio, DirectoryMarker, DirectoryProxy, NodeMarker},
    fidl_fuchsia_logger::{LogSinkMarker, LogSinkProxy},
    fuchsia_async as fasync,
    fuchsia_syslog::{get_fx_logger_level as fx_log_level, Logger},
    fuchsia_zircon as zx,
    futures::future::{AbortHandle, Abortable, BoxFuture},
    log::*,
    std::fmt::Arguments,
    std::{collections::HashMap, path::PathBuf, sync::Arc},
    vfs::{
        directory::entry::DirectoryEntry, directory::helper::DirectlyMutable,
        directory::immutable::simple as pfs, execution_scope::ExecutionScope, path::Path,
    },
};

type Directory = Arc<pfs::Simple>;

pub struct IncomingNamespace {
    pub package_dir: Option<Arc<DirectoryProxy>>,
    dir_abort_handles: Vec<AbortHandle>,
    logger: Option<NamespaceLogger>,
}

/// Writes to a LogSink socket obtained from the LogSinkProtocol. The
/// implementation falls back to using a default logger if the LogSink is
/// unavailable.
pub struct NamespaceLogger {
    // We keep the handle to the service around with the idea we might make use
    // of the service itself in the future.
    _log_sink_protocol: LogSinkProxy,
    logger: Logger,
}

impl NamespaceLogger {
    pub fn get_logger(&self) -> &Logger {
        &self.logger
    }
}

impl FmtArgsLogger for NamespaceLogger {
    fn log(&self, level: Level, args: Arguments) {
        if self.logger.is_connected() {
            self.logger.log_f(fx_log_level(level), args, None);
        } else {
            MODEL_LOGGER.log(level, args);
        }
    }
}

impl Drop for IncomingNamespace {
    fn drop(&mut self) {
        for abort_handle in &self.dir_abort_handles {
            abort_handle.abort();
        }
    }
}

impl IncomingNamespace {
    pub fn new(package: Option<Package>) -> Result<Self, ModelError> {
        let package_dir = package.map(|p| p.package_dir);
        Ok(Self { package_dir, dir_abort_handles: vec![], logger: None })
    }

    /// Returns a Logger whose output is attributed to this component's
    /// namespace.
    pub fn get_attributed_logger(&self) -> Option<&NamespaceLogger> {
        self.logger.as_ref()
    }

    /// Get a logger, an attributed logger is returned if available, otherwise
    /// a default logger whose output belongs to component manager is returned.
    pub fn get_logger(&self) -> &dyn FmtArgsLogger {
        if let Some(attr_logger) = self.logger.as_ref() {
            attr_logger
        } else {
            // MODEL_LOGGER is a lazy_static, which obscures the type, Deref to
            // see through to the type, a reference to which implements
            // FmtArgsLogger
            &MODEL_LOGGER
        }
    }

    /// In addition to populating a Vec<fcrunner::ComponentNamespaceEntry>, `populate` will start
    /// serving and install handles to pseudo directories.
    pub async fn populate<'a>(
        &'a mut self,
        realm: WeakRealm,
        decl: &'a ComponentDecl,
    ) -> Result<Vec<fcrunner::ComponentNamespaceEntry>, ModelError> {
        let mut ns: Vec<fcrunner::ComponentNamespaceEntry> = vec![];

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

        let mut log_sink_decl: Option<UseProtocolDecl> = None;
        for use_ in &decl.uses {
            match use_ {
                cm_rust::UseDecl::Directory(_) => {
                    Self::add_directory_use(&mut ns, &mut directory_waiters, use_, realm.clone())?;
                }
                cm_rust::UseDecl::Protocol(s) => {
                    Self::add_service_use(&mut svc_dirs, s, realm.clone())?;
                    if s.source_name.0 == LogSinkMarker::NAME {
                        log_sink_decl = Some(s.clone());
                    }
                }
                cm_rust::UseDecl::Service(_) => {
                    return Err(ModelError::unsupported("Service capability"));
                }
                cm_rust::UseDecl::Storage(_) => {
                    Self::add_storage_use(&mut ns, &mut directory_waiters, use_, realm.clone())?;
                }
                cm_rust::UseDecl::Runner(_)
                | cm_rust::UseDecl::Event(_)
                | cm_rust::UseDecl::EventStream(_) => {
                    // Event and Runner capabilities are handled in model::model,
                    // as these are capabilities used by the framework itself
                    // and not given to components directly.
                }
            }
        }

        // Start hosting the services directories and add them to the namespace
        self.serve_and_install_svc_dirs(&mut ns, svc_dirs)?;
        self.start_directory_waiters(directory_waiters)?;

        if let Some(log_decl) = &log_sink_decl {
            let (ns_, logger) = self.get_logger_from_ns(ns, log_decl).await;
            ns = ns_;
            self.logger = logger;
        }

        Ok(ns)
    }

    /// Given the set of namespace entries and a LogSink protocol's
    /// `UseProtocolDecl`, look through the namespace for where to connect
    /// to the LogSink protocol. The log connection, if any, is stored in the
    /// IncomingNamespace.
    async fn get_logger_from_ns(
        &self,
        ns: Vec<fcrunner::ComponentNamespaceEntry>,
        log_sink_decl: &UseProtocolDecl,
    ) -> (Vec<fcrunner::ComponentNamespaceEntry>, Option<NamespaceLogger>) {
        // A new set of namespace entries is returned because when the entry
        // used to connect to LogSink is found, that entry is consumed. A
        // matching entry is created and placed in the set of entries returned
        // by this function. `self` is taken as mutable so the
        // logger connection can be stored when found.
        let mut new_ns = vec![];
        let mut log_ns_dir: Option<(fcrunner::ComponentNamespaceEntry, String)> = None;
        let mut logger = Option::<NamespaceLogger>::None;
        // Find namespace directory specified in the log_sink_decl
        for ns_dir in ns {
            if let Some(path) = &ns_dir.path.clone() {
                // Check if this namespace path is a stem of the decl's path
                if log_ns_dir.is_none() {
                    if let Ok(path_remainder) =
                        Self::is_subpath_of(log_sink_decl.target_path.to_string(), path.to_string())
                    {
                        log_ns_dir = Some((ns_dir, path_remainder));
                        continue;
                    }
                }
            }
            new_ns.push(ns_dir);
        }

        // If we found a matching namespace entry, try to open the log proxy
        if let Some((mut entry, remaining_path)) = log_ns_dir {
            if let Some(dir) = entry.directory {
                let _str = log_sink_decl.target_path.to_string();
                let (restored_dir, logger_) = self.get_logger_from_dir(dir, remaining_path).await;
                entry.directory = restored_dir;
                logger = logger_;
            }
            new_ns.push(entry);
        }
        (new_ns, logger)
    }

    /// Given a Directory, connect to the LogSink protocol at the default
    /// location. o
    async fn get_logger_from_dir(
        &self,
        dir: ClientEnd<DirectoryMarker>,
        at_path: String,
    ) -> (Option<ClientEnd<DirectoryMarker>>, Option<NamespaceLogger>) {
        let mut logger = Option::<NamespaceLogger>::None;
        match dir.into_proxy() {
            Ok(dir_proxy) => {
                // Use io_util instead of fuchsia_component::client because
                // `at_path` might be in a subdirectory of `dir`.
                match io_util::open_node(
                    &dir_proxy,
                    &PathBuf::from(at_path.trim_start_matches("/")),
                    fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
                    fidl_fuchsia_io::MODE_TYPE_SERVICE,
                ) {
                    Ok(log_sink_node) => {
                        let mut sink =
                            LogSinkProxy::from_channel(log_sink_node.into_channel().unwrap());
                        if let Ok(log_socket) = connect_to_logger(&mut sink).await.map_err(|e| {
                            info!("LogSink.Connect() failed, logs will be attributed to component manager: {}", e);
                            e
                        }) {
                            logger = Some(NamespaceLogger {
                                _log_sink_protocol: sink,
                                logger: log_socket,
                            });
                        }
                    }
                    Err(e) => {
                        info!("Could not connect to LogSink, logs will be attributed to component manager: {:?}", e);
                    }
                }

                // Now that we have the LogSink and socket, put the LogSink
                // protocol directory back where we found it.
                (
                    dir_proxy.into_channel().map_or_else(
                        |e| {
                            error!("LogSink proxy could not be converted back to channel: {:?}", e);
                            None
                        },
                        |chan| {
                            Some(ClientEnd::<fidl_fuchsia_io::DirectoryMarker>::new(chan.into()))
                        },
                    ),
                    logger,
                )
            }
            Err(e) => {
                info!("Directory client channel could not be turned into proxy: {}", e);
                (None, logger)
            }
        }
    }

    /// add_pkg_directory will add a handle to the component's package under /pkg in the namespace.
    fn add_pkg_directory(
        ns: &mut Vec<fcrunner::ComponentNamespaceEntry>,
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
        ns.push(fcrunner::ComponentNamespaceEntry {
            path: Some(PKG_PATH.to_str().unwrap().to_string()),
            directory: Some(cloned_dir),
            ..fcrunner::ComponentNamespaceEntry::EMPTY
        });
        Ok(())
    }

    /// Adds a directory waiter to `waiters` and updates `ns` to contain a handle for the
    /// directory described by `use_`. Once the channel is readable, the future calls
    /// `route_directory` to forward the channel to the source component's outgoing directory and
    /// terminates.
    fn add_directory_use(
        ns: &mut Vec<fcrunner::ComponentNamespaceEntry>,
        waiters: &mut Vec<BoxFuture<()>>,
        use_: &UseDecl,
        realm: WeakRealm,
    ) -> Result<(), ModelError> {
        Self::add_directory_helper(ns, waiters, use_, realm)
    }

    /// Adds a directory waiter to `waiters` and updates `ns` to contain a handle for the
    /// storage described by `use_`. Once the channel is readable, the future calls
    /// `route_storage` to forward the channel to the source component's outgoing directory and
    /// terminates.
    fn add_storage_use(
        ns: &mut Vec<fcrunner::ComponentNamespaceEntry>,
        waiters: &mut Vec<BoxFuture<()>>,
        use_: &UseDecl,
        realm: WeakRealm,
    ) -> Result<(), ModelError> {
        Self::add_directory_helper(ns, waiters, use_, realm)
    }

    fn add_directory_helper(
        ns: &mut Vec<fcrunner::ComponentNamespaceEntry>,
        waiters: &mut Vec<BoxFuture<()>>,
        use_: &UseDecl,
        realm: WeakRealm,
    ) -> Result<(), ModelError> {
        let target_path =
            use_.path().expect("use decl without path used in add_directory_helper").to_string();
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
            let target_realm = match realm.upgrade() {
                Ok(realm) => realm,
                Err(e) => {
                    error!("failed to upgrade WeakRealm routing use decl `{:?}`: {:?}", &use_, e);
                    return;
                }
            };
            let mut server_end = server_end.into_zx_channel();
            let res = routing::route_use_capability(
                flags,
                fio::MODE_TYPE_DIRECTORY,
                String::new(),
                &use_,
                &target_realm,
                &mut server_end,
            )
            .await;
            if let Err(e) = res {
                let cap = ComponentCapability::Use(use_);
                let execution = target_realm.lock_execution().await;
                let logger = match &execution.runtime {
                    Some(Runtime { namespace: Some(ns), .. }) => Some(ns.get_logger()),
                    _ => None,
                };
                routing::report_routing_failure(
                    &target_realm.abs_moniker,
                    &cap,
                    &e,
                    server_end,
                    logger,
                );
            }
        };

        waiters.push(Box::pin(route_on_usage));
        ns.push(fcrunner::ComponentNamespaceEntry {
            path: Some(target_path.clone()),
            directory: Some(client_end),
            ..fcrunner::ComponentNamespaceEntry::EMPTY
        });
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
            fasync::Task::spawn(async move {
                let _ = future.await;
            })
            .detach();
        }
        Ok(())
    }

    /// Adds a service broker in `svc_dirs` for service described by `use_`. The service will be
    /// proxied to the outgoing directory of the source component.
    fn add_service_use(
        svc_dirs: &mut HashMap<String, Directory>,
        use_: &cm_rust::UseProtocolDecl,
        realm: WeakRealm,
    ) -> Result<(), ModelError> {
        let use_clone = use_.clone();
        // Used later to attach a not found handler to namespace directories.
        let not_found_realm_copy = realm.clone();
        let route_open_fn = Box::new(
            move |flags: u32,
                  mode: u32,
                  relative_path: String,
                  server_end: ServerEnd<NodeMarker>| {
                let use_ = UseDecl::Protocol(use_clone.clone());
                let realm = realm.clone();
                fasync::Task::spawn(async move {
                    let target_realm = match realm.upgrade() {
                        Ok(realm) => realm,
                        Err(e) => {
                            error!(
                                "failed to upgrade WeakRealm routing use decl `{:?}`: {:?}",
                                &use_, e
                            );
                            return;
                        }
                    };
                    let mut server_end = server_end.into_channel();
                    let res = routing::route_use_capability(
                        flags,
                        mode,
                        relative_path,
                        &use_,
                        &target_realm,
                        &mut server_end,
                    )
                    .await;
                    if let Err(e) = res {
                        let cap = ComponentCapability::Use(use_);
                        let execution = target_realm.lock_execution().await;
                        let logger = match &execution.runtime {
                            Some(Runtime { namespace: Some(ns), .. }) => Some(ns.get_logger()),
                            _ => None,
                        };

                        routing::report_routing_failure(
                            &target_realm.abs_moniker,
                            &cap,
                            &e,
                            server_end,
                            logger,
                        );
                    }
                })
                .detach();
            },
        );

        let service_dir = svc_dirs.entry(use_.target_path.dirname.clone()).or_insert_with(|| {
            let new_dir = pfs::simple();
            // Grab a copy of the directory path, it will be needed if we log a
            // failed open request.
            let dir_path = use_.target_path.dirname.clone();
            new_dir.clone().set_not_found_handler(Box::new(move |path| {
                // Clone the realm pointer and pass the copy into the logger.
                let realm_for_logger = not_found_realm_copy.clone();
                let requested_path = format!("{}/{}", dir_path, path);

                // Spawn a task which logs the error. It would be nicer to not
                // spawn a task, but locking the realm is async and this
                // closure is not.
                fasync::Task::spawn(async move {
                    match realm_for_logger.upgrade() {
                        Ok(target_realm) => {
                            let execution = target_realm.lock_execution().await;
                            let logger = match &execution.runtime {
                                Some(Runtime { namespace: Some(ns), .. }) => ns.get_logger(),
                                _ => &MODEL_LOGGER,
                            };
                            logger.log(
                                Level::Warn,
                                format_args!(
                                    "No capability available at path {} for component {}, \
                                     verify the component has the proper `use` declaration.",
                                    requested_path, target_realm.abs_moniker
                                ),
                            );
                        }
                        Err(_) => {}
                    }
                })
                .detach();
            }));
            new_dir
        });
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
        ns: &mut Vec<fcrunner::ComponentNamespaceEntry>,
        svc_dirs: HashMap<String, Directory>,
    ) -> Result<(), ModelError> {
        for (target_dir_path, pseudo_dir) in svc_dirs {
            let (client_end, server_end) =
                create_endpoints::<NodeMarker>().expect("could not create node proxy endpoints");
            pseudo_dir.clone().open(
                ExecutionScope::new(),
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                Path::empty(),
                server_end.into_channel().into(),
            );

            ns.push(fcrunner::ComponentNamespaceEntry {
                path: Some(target_dir_path.as_str().to_string()),
                directory: Some(ClientEnd::new(client_end.into_channel())), // coerce to ClientEnd<Dir>
                ..fcrunner::ComponentNamespaceEntry::EMPTY
            });
        }
        Ok(())
    }

    /// Determines if the `full` is a subpath of the `stem`. Returns the
    /// remaining portion of the path if `full` us a subpath. Returns Error if
    /// `stem` and `full` are the same.
    fn is_subpath_of(full: String, stem: String) -> Result<String, ()> {
        let stem_path = std::path::Path::new(&stem);
        let full_path = std::path::Path::new(&full);

        let remainder = full_path
            .strip_prefix(stem_path)
            // Unwrapping the `to_str` conversion should be safe here since we
            // started with a Unicode value, put it into a path and now are
            // extracting a portion of that value.
            .map(|path| path.to_str().unwrap().to_string())
            .map_err(|_| ())?;

        if remainder.is_empty() {
            Err(())
        } else {
            Ok(remainder)
        }
    }
}

async fn connect_to_logger(sink: &fidl_fuchsia_logger::LogSinkProxy) -> Result<Logger, Error> {
    let (tx, rx) = zx::Socket::create(zx::SocketOpts::DATAGRAM)
        .context("socket creation for logger connection failed")?;
    sink.connect(rx).context("protocool error connecting to logger socket")?;
    // TODO fxbug.dev/62644
    // IMPORTANT: Set tags to empty so attributed tags will be generated by the
    // logger. The logger should see this capability request as coming from the
    // component and use its default tags for messages coming via this socket.
    let tags = vec![];
    let logger = fuchsia_syslog::build_with_tags_and_socket(tx, &tags)
        .context("logger could not be built")?;
    Ok(logger)
}

#[cfg(test)]
pub mod test {

    use {
        super::IncomingNamespace,
        cm_rust::{CapabilityPath, UseProtocolDecl, UseSource},
        fidl::endpoints::{self, ServiceMarker},
        fidl_fuchsia_component_runner as fcrunner,
        fidl_fuchsia_logger::{LogSinkMarker, LogSinkRequest, LogSinkRequestStream},
        fuchsia_async,
        fuchsia_component::server::ServiceFs,
        futures::StreamExt,
        std::{
            convert::TryFrom,
            sync::{Arc, Mutex},
        },
    };

    #[test]
    fn test_subpath_handling() {
        let mut stem = "/".to_string();
        let mut full = "/".to_string();
        assert_eq!(IncomingNamespace::is_subpath_of(full, stem), Err(()));

        stem = "/".to_string();
        full = "/subdir".to_string();
        assert_eq!(IncomingNamespace::is_subpath_of(full, stem), Ok("subdir".to_string()));

        stem = "/subdir1/subdir2".to_string();
        full = "/subdir1/file.txt".to_string();
        assert_eq!(IncomingNamespace::is_subpath_of(full, stem), Err(()));

        stem = "/this/path/has/a/typ0".to_string();
        full = "/this/path/has/a/typo/not/exclamation/point".to_string();
        assert_eq!(IncomingNamespace::is_subpath_of(full, stem), Err(()));

        stem = "/subdir1".to_string();
        full = "/subdir1/subdir2/subdir3/file.txt".to_string();
        assert_eq!(
            IncomingNamespace::is_subpath_of(full, stem),
            Ok("subdir2/subdir3/file.txt".to_string())
        );
    }

    enum FidlServices {
        LogSink(LogSinkRequestStream),
    }

    #[fuchsia_async::run_singlethreaded(test)]
    /// Tests that the logger is connected to when it is in a subdirectory of a
    /// namespace entry.
    async fn test_logger_at_root_of_entry() {
        let incoming_ns = IncomingNamespace::new(None).expect("namespace failed to create");
        let log_decl = UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "logsink".into(),
            target_path: CapabilityPath::try_from("/fuchsia.logger.LogSink").unwrap(),
        };

        let (dir_client, dir_server) =
            endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
                .expect("failed to create VFS endpoints");
        let mut root_dir = ServiceFs::new_local();
        root_dir.add_fidl_service_at(LogSinkMarker::NAME, FidlServices::LogSink);
        let _sub_dir = root_dir.dir("subdir");
        root_dir
            .serve_connection(dir_server.into_channel())
            .expect("failed to add serving channel");

        let ns_entries = vec![fcrunner::ComponentNamespaceEntry {
            path: Some("/".to_string()),
            directory: Some(dir_client),
            ..fcrunner::ComponentNamespaceEntry::EMPTY
        }];

        verify_logger_connects_in_namespace(
            Some(&mut root_dir),
            incoming_ns,
            ns_entries,
            log_decl,
            true,
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    /// Tests that the logger is connected to when it is in a subdirectory of a
    /// namespace entry.
    async fn test_logger_at_subdir_of_entry() {
        let incoming_ns = IncomingNamespace::new(None).expect("namespace failed to create");
        let log_decl = UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "logsink".into(),
            target_path: CapabilityPath::try_from("/arbitrary-dir/fuchsia.logger.LogSink").unwrap(),
        };

        let (dir_client, dir_server) =
            endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
                .expect("failed to create VFS endpoints");
        let mut root_dir = ServiceFs::new_local();
        let mut svc_dir = root_dir.dir("arbitrary-dir");
        svc_dir.add_fidl_service_at(LogSinkMarker::NAME, FidlServices::LogSink);
        let _sub_dir = root_dir.dir("subdir");
        root_dir
            .serve_connection(dir_server.into_channel())
            .expect("failed to add serving channel");

        let ns_entries = vec![fcrunner::ComponentNamespaceEntry {
            path: Some("/".to_string()),
            directory: Some(dir_client),
            ..fcrunner::ComponentNamespaceEntry::EMPTY
        }];

        verify_logger_connects_in_namespace(
            Some(&mut root_dir),
            incoming_ns,
            ns_entries,
            log_decl,
            true,
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multiple_namespace_entries() {
        let incoming_ns = IncomingNamespace::new(None).expect("namespace failed to create");
        let log_decl = UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "logsink".into(),
            target_path: CapabilityPath::try_from("/svc/fuchsia.logger.LogSink").unwrap(),
        };

        let (dir_client, dir_server) =
            endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
                .expect("failed to create VFS endpoints");
        let mut root_dir = ServiceFs::new_local();
        root_dir.add_fidl_service_at(LogSinkMarker::NAME, FidlServices::LogSink);
        let _sub_dir = root_dir.dir("subdir");
        root_dir
            .serve_connection(dir_server.into_channel())
            .expect("failed to add serving channel");

        // Create a directory for another namespace entry which we don't
        // actually expect to be accessed.
        let (extra_dir_client, extra_dir_server) =
            endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
                .expect("Failed creating directory endpoints");
        let mut extra_dir = ServiceFs::new_local();
        extra_dir.add_fidl_service(FidlServices::LogSink);
        extra_dir
            .serve_connection(extra_dir_server.into_channel())
            .expect("serving channel failed");

        let ns_entries = vec![
            fcrunner::ComponentNamespaceEntry {
                path: Some("/svc".to_string()),
                directory: Some(dir_client),
                ..fcrunner::ComponentNamespaceEntry::EMPTY
            },
            fcrunner::ComponentNamespaceEntry {
                path: Some("/sv".to_string()),
                directory: Some(extra_dir_client),
                ..fcrunner::ComponentNamespaceEntry::EMPTY
            },
        ];

        verify_logger_connects_in_namespace(
            Some(&mut root_dir),
            incoming_ns,
            ns_entries,
            log_decl,
            true,
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_connect_on_empty_namespace() {
        let incoming_ns = IncomingNamespace::new(None).expect("namespace failed to create");
        let log_decl = UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "logsink".into(),
            target_path: CapabilityPath::try_from("/svc/fuchsia.logger.LogSink").unwrap(),
        };

        let ns_entries = vec![];

        verify_logger_connects_in_namespace(
            Option::<&mut ServiceFs<fuchsia_component::server::ServiceObjLocal<FidlServices>>>::None,
            incoming_ns,
            ns_entries,
            log_decl,
            false,
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_logsink_dir_not_in_namespace() {
        let incoming_ns = IncomingNamespace::new(None).expect("namespace failed to create");
        let log_decl = UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "logsink".into(),
            target_path: CapabilityPath::try_from("/svc/fuchsia.logger.LogSink").unwrap(),
        };

        let (dir_client, dir_server) =
            endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()
                .expect("failed to create VFS endpoints");
        let mut root_dir = ServiceFs::new_local();
        root_dir.add_fidl_service_at(LogSinkMarker::NAME, FidlServices::LogSink);
        root_dir
            .serve_connection(dir_server.into_channel())
            .expect("failed to add serving channel");

        let ns_entries = vec![fcrunner::ComponentNamespaceEntry {
            path: Some("/not-the-svc-dir".to_string()),
            directory: Some(dir_client),
            ..fcrunner::ComponentNamespaceEntry::EMPTY
        }];

        verify_logger_connects_in_namespace(
            Some(&mut root_dir),
            incoming_ns,
            ns_entries,
            log_decl,
            false,
        )
        .await;
    }

    /// Verify the expected logger connection behavior and that the logger is
    /// set or not in the namespace.
    async fn verify_logger_connects_in_namespace<
        T: fuchsia_component::server::ServiceObjTrait<Output = FidlServices>,
    >(
        root_dir: Option<&mut ServiceFs<T>>,
        incoming_ns: IncomingNamespace,
        ns_entries: Vec<fcrunner::ComponentNamespaceEntry>,
        proto_decl: UseProtocolDecl,
        connects: bool,
    ) {
        let connection_count = if connects { 1u8 } else { 0u8 };
        // Create a task that will access the namespace by calling
        // `get_logger_from_ns`. This task won't complete until the VFS backing
        // the namespace starts responding to requests. This VFS is served by
        // code in the next stanza.
        fuchsia_async::Task::spawn(async move {
            let ns_size = ns_entries.len();
            let (procesed_ns, logger) =
                incoming_ns.get_logger_from_ns(ns_entries, &proto_decl).await;
            assert_eq!(logger.is_some(), connects);
            assert_eq!(ns_size, procesed_ns.len())
        })
        .detach();

        if let Some(dir) = root_dir {
            // Serve the directory and when the LogSink service is requested
            // provide a closure that counts number of calls to the Connect
            // method. Serving stops when the spawned task drops the
            // IncomingNamespace, which holds the other side of the VFS
            // directory handle.
            let request_count = Arc::new(Mutex::new(0u8));
            let request_count_copy = request_count.clone();
            dir.for_each_concurrent(10usize, move |request: FidlServices| match request {
                FidlServices::LogSink(mut r) => {
                    let req_count = request_count_copy.clone();
                    async move {
                        match r.next().await.expect("stream error").expect("fidl error") {
                            LogSinkRequest::Connect { .. } => {
                                let mut count = req_count.lock().expect("locking failed");
                                *count += 1;
                            }
                            LogSinkRequest::ConnectStructured { .. } => {
                                panic!("Unexpected call to `ConnectStructured`");
                            }
                        }
                    }
                }
            })
            .await;
            assert_eq!(*request_count.lock().expect("lock failed"), connection_count);
        }
    }
}
