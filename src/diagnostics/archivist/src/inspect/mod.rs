// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The collection module consists of methods and structures around two
//! different types of event streams: ComponentEvent and HubEvent.
//!
//! HubEvents are used internally for the HubCollector to attach watchers
//! for various different types in the Hub hierarchy. ComponentEvents are
//! exposed externally over a stream for use by the Archivist.

use {
    crate::collection::*,
    crate::selectors,
    failure::{self, err_msg, format_err, Error},
    fidl_fuchsia_diagnostics_inspect::{
        DisplaySettings, FormatSettings, ReaderError, ReaderRequest, ReaderRequestStream,
        ReaderSelector, Selector, TextSettings,
    },
    fidl_fuchsia_io::{DirectoryProxy, NodeInfo, CLONE_FLAG_SAME_RIGHTS},
    fidl_fuchsia_mem, files_async, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::future::{join_all, BoxFuture},
    futures::{future, FutureExt, TryFutureExt, TryStreamExt},
    io_util,
    std::path::{Path, PathBuf},
    std::sync::{Arc, Mutex},
};

/// InspectDataCollector holds the information needed to retrieve the Inspect
/// VMOs associated with a particular component
#[derive(Clone, Debug)]
pub struct InspectDataCollector {
    /// The inspect data associated with a particular event.
    ///
    /// This is wrapped in an Arc Mutex so it can be shared between multiple data sources.
    ///
    /// Note: The Arc is needed so that we can both add the data map to a data collector
    ///       and trigger async collection of the data in the same method. This can only
    ///       be done by allowing the async method to populate the same data that is being
    ///       passed into the component event.
    inspect_data_map: Arc<Mutex<Option<DataMap>>>,
}

impl InspectDataCollector {
    /// Construct a new InspectDataCollector, wrapped by an Arc<Mutex>.
    pub fn new() -> Self {
        InspectDataCollector { inspect_data_map: Arc::new(Mutex::new(Some(DataMap::new()))) }
    }

    /// Convert a fully-qualified path to a directory-proxy in the executing namespace.
    /// NOTE: Currently does a synchronous directory-open, since there are no available
    ///       async apis.
    pub async fn find_directory_proxy(path: &PathBuf) -> Result<DirectoryProxy, Error> {
        // TODO(36762): When available, use the async directory-open api.
        return io_util::open_directory_in_namespace(
            &path.to_string_lossy(),
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        );
    }

    /// Searches the directory specified by inspect_directory_proxy for
    /// .inspect files and populates the `inspect_data_map` with the found VMOs.
    pub async fn populate_data_map(&mut self, inspect_proxy: &DirectoryProxy) -> Result<(), Error> {
        // TODO(36762): Use a streaming and bounded readdir API when available to avoid
        // being hung.
        for entry in files_async::readdir_recursive(inspect_proxy).await?.into_iter() {
            // We are only currently interested in inspect files.
            if !entry.name.ends_with(".inspect") || entry.kind != files_async::DirentKind::File {
                continue;
            }

            let file_proxy = match io_util::open_file(
                inspect_proxy,
                Path::new(&entry.name),
                io_util::OPEN_RIGHT_READABLE,
            ) {
                Ok(proxy) => proxy,
                Err(_) => {
                    continue;
                }
            };

            // Obtain the vmo backing any VmoFiles.
            match file_proxy.describe().await {
                Ok(nodeinfo) => match nodeinfo {
                    NodeInfo::Vmofile(vmofile) => {
                        self.maybe_add(
                            Path::new(&entry.name)
                                .file_name()
                                .unwrap()
                                .to_string_lossy()
                                .to_string(),
                            Data::Vmo(vmofile.vmo),
                        );
                    }
                    _ => {}
                },
                Err(_) => {}
            }
        }

        Ok(())
    }

    /// Adds a key value to the contained vector if it hasn't been taken yet. Otherwise, does
    /// nothing.
    fn maybe_add(&mut self, key: String, value: Data) {
        let mut data_map = self.inspect_data_map.lock().unwrap();
        match data_map.as_mut() {
            Some(map) => {
                map.insert(key, value);
            }
            _ => {}
        };
    }
}

impl DataCollector for InspectDataCollector {
    /// Takes the contained extra data. Additions following this have no effect.
    fn take_data(self: Box<Self>) -> Option<DataMap> {
        self.inspect_data_map.lock().unwrap().take()
    }

    /// Collect extra data stored under the given path.
    ///
    /// This currently only does a single pass over the directory to find information.
    fn collect(mut self: Box<Self>, path: PathBuf) -> BoxFuture<'static, Result<(), Error>> {
        async move {
            let inspect_proxy = match InspectDataCollector::find_directory_proxy(&path).await {
                Ok(proxy) => proxy,
                Err(e) => {
                    return Err(format_err!("Failed to open out directory at {:?}: {}", path, e));
                }
            };

            return self.populate_data_map(&inspect_proxy).await;
        }
            .boxed()
    }
}

/// InspectDataRepository manages storage of all state needed in order
/// for the inspect reader to retrieve inspect data when a read is requested.
pub struct InspectDataRepository {
    // TODO(lukenicholson): Wrap directory proxies in a trie of
    // component names to make filtering by selectors work.
    data_directories: Vec<(PathBuf, DirectoryProxy)>,
}

impl InspectDataRepository {
    pub fn new() -> Self {
        InspectDataRepository { data_directories: Vec::new() }
    }

    pub fn add(&mut self, component_hierachy_path: PathBuf, directory_proxy: DirectoryProxy) {
        self.data_directories.push((component_hierachy_path, directory_proxy));
    }

    /// Return all of the DirectoryProxies that contain Inspect hierarchies
    /// which contain data that should be selected from.
    pub fn fetch_data(&self) -> Vec<DirectoryProxy> {
        return self
            .data_directories
            .iter()
            .filter_map(|(_, y)| match io_util::clone_directory(y, CLONE_FLAG_SAME_RIGHTS) {
                Ok(directory) => Some(directory),
                Err(_) => None,
            })
            .collect();
    }
}

/// ReaderServer holds the state and data needed to serve Inspect data
/// reading requests for a single client.
///
/// active_selectors: are the vector of selectors which are configuring what
///                   inspect data is returned by read requests.
///
/// inspect_repo: the InspectDataRepository which holds the access-points for all relevant
///               inspect data.
#[derive(Clone)]
pub struct ReaderServer {
    pub active_selectors: Arc<Mutex<Vec<Selector>>>,
    pub inspect_repo: Arc<Mutex<InspectDataRepository>>,
}

impl ReaderServer {
    pub fn new(inspect_repo: Arc<Mutex<InspectDataRepository>>) -> Self {
        ReaderServer {
            inspect_repo: inspect_repo,
            active_selectors: Arc::new(Mutex::new(Vec::new())),
        }
    }

    /// Add a new selector to the active-selectors list.
    /// Requires: The active_selectors lock is free.
    ///           `reader_selector` is a valid formatted or plaintext inspect selector.
    pub fn add_selector(&self, reader_selector: ReaderSelector) -> Result<(), ReaderError> {
        match reader_selector {
            ReaderSelector::StructuredSelector(x) => self.active_selectors.lock().unwrap().push(x),
            ReaderSelector::StringSelector(x) => match selectors::parse_selector(&x) {
                Ok(parsed_selector) => self.active_selectors.lock().unwrap().push(parsed_selector),
                Err(_) => {
                    return Err(ReaderError::InvalidSelector);
                }
            },
            _ => {
                return Err(ReaderError::InvalidSelector);
            }
        }
        Ok(())
    }

    /// Removes all selectors that were previously configured for the session.
    /// This puts the server back into a state where read requests return all
    /// data that is exposed through the service.
    pub fn clear_selectors(&mut self) {
        self.active_selectors.lock().unwrap().clear();
    }

    /// Reads all relevant inspect data based off of the active_selectors
    /// and data exposed to the service, and formats it into a single text dump
    /// with format controlled by `text_settings`.
    ///
    /// Returns: a Buffer to a READ_ONLY VMO containing the text dump.
    // TODO(lukenicholson): Actually format using text_settings.
    async fn format_text(
        self,
        _text_settings: TextSettings,
    ) -> Result<(fidl_fuchsia_mem::Buffer), Error> {
        // We must fetch the repositories in a closure to prevent the
        // repository mutex-guard from leaking into the futures.
        let inspect_data_directories = {
            let locked_inspect_repo = self.inspect_repo.lock().unwrap();
            locked_inspect_repo.fetch_data()
        };

        let mut populated_inspect_collectors: Vec<Result<InspectDataCollector, Error>> =
            join_all(inspect_data_directories.into_iter().map(move |dir_proxy| {
                async move {
                    let mut collector = InspectDataCollector::new();
                    match collector.populate_data_map(&dir_proxy).await {
                        Ok(_) => return Ok(collector),
                        Err(e) => return Err(e),
                    };
                }
            }))
            .await;

        let aggregated_vmo_string = populated_inspect_collectors.drain(0..).fold(
            String::new(),
            |mut accumulator, populated_collector| {
                match populated_collector {
                    Ok(collector) => {
                        let collector: Box<dyn DataCollector> = Box::new(collector);
                        collector.take_data().and_then(|data_map| {
                            data_map.into_iter().for_each(|(inspect_vmo_name, data)| match data {
                                Data::Vmo(vmo) => {
                                    let new_inspect_string = format!(
                                        "\n{name}:\n{vmo:#?}",
                                        name = inspect_vmo_name,
                                        vmo = vmo
                                    );
                                    accumulator.push_str(&new_inspect_string);
                                }
                                Data::Empty => {}
                            });
                            Some(())
                        });
                        accumulator
                    }

                    // TODO(36761): What does it mean for IO to fail on a
                    // subset of directory data collections?
                    Err(_) => accumulator,
                }
            },
        );

        let vmo_size: u64 = aggregated_vmo_string.len() as u64;

        // TODO(lukenicholson): Inspect dumps may be large enough that they should be split
        // over multiple VMOs and streamed to the client
        let dump_vmo = zx::Vmo::create(vmo_size as u64)
            .map_err(|s| err_msg(format!("error creating buffer, zx status: {}", s)))?;

        dump_vmo
            .write(aggregated_vmo_string.as_bytes(), 0)
            .map_err(|s| err_msg(format!("error writing buffer, zx status: {}", s)))?;

        let client_vmo = dump_vmo.duplicate_handle(zx::Rights::READ | zx::Rights::BASIC)?;
        Ok(fidl_fuchsia_mem::Buffer { vmo: client_vmo, size: vmo_size })
    }

    /// Reads all relevant inspect data based off of the active_selectors
    /// and data exposed to the service, and formats it
    /// with format controlled by `settings`, before dumping it into a VMO
    /// to be returned to the client.
    ///
    /// Returns: a Buffer to a READ_ONLY VMO containing the formatted data dump.
    pub async fn format(self, settings: FormatSettings) -> Result<fidl_fuchsia_mem::Buffer, Error> {
        let display_settings = match settings.format {
            Some(display_format) => display_format,
            None => {
                return Err(format_err!(
                    "The FormatSettings were missing required display settings."
                ))
            }
        };

        match display_settings {
            DisplaySettings::Json(_) => {
                return Err(format_err!("Json formatting is currently not supported."))
            }
            DisplaySettings::Text(text_settings) => return self.format_text(text_settings).await,
            _ => unreachable!("This branch should never be taken."),
        }
    }

    pub fn spawn_reader_server(mut self, stream: ReaderRequestStream) {
        fasync::spawn(
            stream
                .try_for_each(move |req| {
                    future::ready(match req {
                        ReaderRequest::AddSelector { selector, responder } => {
                            responder.send(&mut self.add_selector(selector))
                        }
                        ReaderRequest::ClearSelectors { control_handle: _ } => {
                            self.clear_selectors();
                            Ok(())
                        }
                        ReaderRequest::Format { settings, responder } => {
                            let server_clone = self.clone();

                            fasync::spawn(async move {
                                match server_clone.format(settings).await {
                                    Ok(vmo_buffer) => {
                                        match responder.send(&mut Ok(vmo_buffer)) {
                                            // TODO(lukenicholson): What does a failed
                                            // response mean here?
                                            _ => return,
                                        };
                                    }
                                    _ => {
                                        match responder.send(&mut Err(ReaderError::Io)) {
                                            // TODO(lukenicholson): What does a failed
                                            // response mean here?
                                            _ => return,
                                        };
                                    }
                                }
                            });

                            Ok(())
                        }
                    })
                })
                .map_ok(|_| ())
                .unwrap_or_else(|e| eprintln!("error running inspect server: {:?}", e)),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::collection::DataCollector, fdio, fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs, fuchsia_zircon as zx, fuchsia_zircon::Peered,
        futures::StreamExt,
    };

    #[fasync::run_singlethreaded(test)]
    async fn inspect_data_collector() {
        let path = PathBuf::from("/test-bindings");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        vmo.write(b"test", 0).unwrap();
        let vmo2 = zx::Vmo::create(4096).unwrap();
        vmo2.write(b"test", 0).unwrap();
        fs.dir("objects").add_vmo_file_at("root.inspect", vmo, 0, 4096);
        fs.dir("objects").add_vmo_file_at("root_not_inspect", vmo2, 0, 4096);

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());

        let (done0, done1) = zx::Channel::create().unwrap();

        let thread_path = path.join("out");
        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let collector = InspectDataCollector::new();

                // Trigger collection on a clone of the inspect collector so
                // we can use collector to take the collected data.
                Box::new(collector.clone()).collect(path).await.unwrap();
                let collector: Box<InspectDataCollector> = Box::new(collector);

                let extra_data = collector.take_data().expect("collector missing data");
                assert_eq!(1, extra_data.len());

                let extra = extra_data.get("root.inspect");
                assert!(extra.is_some());

                match extra.unwrap() {
                    Data::Vmo(vmo) => {
                        let mut buf = [0u8; 4];
                        vmo.read(&mut buf, 0).expect("reading vmo");
                        assert_eq!(b"test", &buf);
                    }
                    v => {
                        panic!("Expected Vmo, got {:?}", v);
                    }
                }

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn reader_server_formatting() {
        let path = PathBuf::from("/test-bindings2");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        vmo.write(b"test", 0).unwrap();
        fs.dir("objects").add_vmo_file_at("root.inspect", vmo, 0, 4096);

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());

        let (done0, done1) = zx::Channel::create().unwrap();

        let thread_path = path.join("out");
        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let mut inspect_repo = InspectDataRepository::new();

                let out_dir_proxy =
                    InspectDataCollector::find_directory_proxy(&path).await.unwrap();

                inspect_repo.add(path, out_dir_proxy);

                let reader_server = ReaderServer::new(Arc::new(Mutex::new(inspect_repo)));

                let format_settings = FormatSettings {
                    format: Some(DisplaySettings::Text(TextSettings { indent: 4 })),
                };

                let inspect_data_dump = reader_server.format(format_settings).await.unwrap();
                let mut buf = vec![0; inspect_data_dump.size as usize];
                inspect_data_dump.vmo.read(&mut buf, 0).expect("reading vmo");

                // Until we have an API for dumping inspect vmos in formatted ways, we rely on
                // the vmo debug logic, which currently includes the handle number.
                // So the full dump would look like:
                // "root.inspect:
                //  Vmo(
                //      Handle(
                //             3930929735,
                //            ),
                //     )"
                // Which we cant match literally on since handle numbers change between runs.
                // So we match on the start and end.
                assert!(std::str::from_utf8(&buf)
                    .unwrap()
                    .starts_with("\nroot.inspect:\nVmo(\n    Handle(\n        "));

                assert!(std::str::from_utf8(&buf).unwrap().ends_with(",\n    ),\n)"));

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }
}
