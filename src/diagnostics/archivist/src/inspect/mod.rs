// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::collection::*,
    crate::selector_evaluator,
    crate::selectors,
    failure::{self, err_msg, format_err, Error},
    fidl_fuchsia_diagnostics_inspect::{
        DisplaySettings, FormatSettings, ReaderError, ReaderRequest, ReaderRequestStream,
        ReaderSelector, Selector, TextSettings,
    },
    fidl_fuchsia_io::{DirectoryProxy, NodeInfo, CLONE_FLAG_SAME_RIGHTS},
    fidl_fuchsia_mem, files_async, fuchsia_async as fasync,
    fuchsia_inspect::reader::{snapshot::Snapshot, NodeHierarchy},
    fuchsia_inspect::trie,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::future::{join_all, BoxFuture},
    futures::{future, FutureExt, TryFutureExt, TryStreamExt},
    inspect_formatter::{self, HierarchyData, HierarchyFormatter, JsonFormatter},
    io_util,
    regex::{Regex, RegexSet},
    std::convert::TryFrom,
    std::path::{Path, PathBuf},
    std::sync::{Arc, Mutex, RwLock},
};

type InspectDataTrie = trie::Trie<char, (PathBuf, DirectoryProxy, RegexSet, Vec<Regex>)>;

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
    pub async fn find_directory_proxy(path: &Path) -> Result<DirectoryProxy, Error> {
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

/// InspectDataContainer is the container that holds
/// all information needed to interact with the inspect
/// hierarchies under a component's out directory.
pub struct InspectDataContainer {
    /// Path to the out directory that this
    /// data packet is configured for.
    component_out_dir_path: PathBuf,
    /// DirectoryProxy for the out directory that this
    /// data packet is configured for.
    component_out_proxy: DirectoryProxy,
    /// RegexSet encoding all the node path selectors for
    /// inspect hierarchies under this component's out directory.
    component_node_selector: RegexSet,
    /// Vector of Regexes corresponding to the node path selectors
    /// in the regex set.
    /// Note: Order of Regexes matters here, this vector must be aligned
    /// with the vector used to construct component_node_selector since
    /// conponent_node_selector.matches() returns a vector of ints used to
    /// find all the relevant property selectors corresponding to the matching
    /// node selectors.
    node_property_selectors: Vec<Regex>,
}

/// InspectDataRepository manages storage of all state needed in order
/// for the inspect reader to retrieve inspect data when a read is requested.
pub struct InspectDataRepository {
    // TODO(lukenicholson): Wrap directory proxies in a trie of
    // component names to make filtering by selectors work.
    data_directories: InspectDataTrie,
    static_selectors: Vec<Arc<Selector>>,
}

impl InspectDataRepository {
    pub fn new(static_selectors: Vec<Arc<Selector>>) -> Self {
        if static_selectors.is_empty() {
            panic!(
                "We require all inspect repositories to be explicit about the data they select."
            );
        }

        InspectDataRepository {
            data_directories: InspectDataTrie::new(),
            static_selectors: static_selectors,
        }
    }

    pub fn add(
        &mut self,
        component_name: String,
        absolute_moniker: Vec<String>,
        component_hierachy_path: PathBuf,
        directory_proxy: DirectoryProxy,
    ) -> Result<(), Error> {
        let matched_selectors = selector_evaluator::match_component_moniker_against_selectors(
            &absolute_moniker,
            &self.static_selectors,
        );
        match matched_selectors {
            Ok(selectors) => {
                if !selectors.is_empty() {
                    let node_path_regexes = selectors
                        .iter()
                        .map(|selector| match &selector.tree_selector.node_path {
                            Some(node_path) => selectors::convert_path_selector_to_regex(node_path),
                            None => unreachable!("Selectors are required to specify a node path."),
                        })
                        .collect::<Result<Vec<Regex>, Error>>()?;

                    let node_path_regex_set = RegexSet::new(
                        &node_path_regexes
                            .iter()
                            .map(|selector_regex| selector_regex.as_str())
                            .collect::<Vec<&str>>(),
                    )?;

                    let property_regexes = selectors
                        .iter()
                        .map(|selector| match &selector.tree_selector.target_properties {
                            Some(target_property) => {
                                selectors::convert_property_selector_to_regex(target_property)
                            }
                            None => unreachable!("Selectors are required to specify a node path."),
                        })
                        .collect::<Result<Vec<Regex>, Error>>()?;

                    self.data_directories.insert(
                        component_name.chars().collect(),
                        (
                            component_hierachy_path,
                            directory_proxy,
                            node_path_regex_set,
                            property_regexes,
                        ),
                    );
                }
                Ok(())
            }
            Err(e) => Err(format_err!("Absoute moniker matching encountered error: {}.", e)),
        }
    }

    /// Return all of the DirectoryProxies that contain Inspect hierarchies
    /// which contain data that should be selected from.
    pub fn fetch_data(&self) -> Vec<InspectDataContainer> {
        return self
            .data_directories
            .iter()
            .filter_map(
                |(_, (component_path, dir_proxy, node_path_regex_set, property_regex_vec))| {
                    io_util::clone_directory(&dir_proxy, CLONE_FLAG_SAME_RIGHTS).ok().map(
                        |directory| InspectDataContainer {
                            component_out_dir_path: component_path.clone(),
                            component_out_proxy: directory,
                            component_node_selector: node_path_regex_set.clone(),
                            node_property_selectors: property_regex_vec.clone(),
                        },
                    )
                },
            )
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
    pub inspect_repo: Arc<RwLock<InspectDataRepository>>,
}

impl ReaderServer {
    pub fn new(inspect_repo: Arc<RwLock<InspectDataRepository>>) -> Self {
        ReaderServer { inspect_repo, active_selectors: Arc::new(Mutex::new(Vec::new())) }
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

    /// Parses an inspect Snapshot into a node hierarchy, and iterates over the
    /// hierarchy creating a copy with selector filters applied.
    fn filter_inspect_snapshot(
        inspect_snapshot: Snapshot,
        path_selectors: &RegexSet,
        property_selectors: &Vec<Regex>,
    ) -> Result<NodeHierarchy, Error> {
        let root_node = NodeHierarchy::try_from(inspect_snapshot)?;
        let mut new_root = NodeHierarchy::new_root();
        for (node_path, property) in root_node.property_iter() {
            let mut formatted_node_path =
                node_path.iter().map(|s| s.as_str()).collect::<Vec<&str>>().join("/");
            // We must append a "/" because the absolute monikers end in slash and
            // hierarchy node paths don't, but we want to reuse the regex logic.
            formatted_node_path.push('/');
            let matching_indices: Vec<usize> =
                path_selectors.matches(&formatted_node_path).into_iter().collect();
            let mut property_regex_strings: Vec<&str> = Vec::new();

            // TODO(4601): We only need to recompile the property selector regex
            // if our iteration brings us to a new node.
            for property_index in matching_indices {
                let property_selector: &Regex = &property_selectors[property_index];
                property_regex_strings.push(property_selector.as_str());
            }

            let property_regex_set = RegexSet::new(property_regex_strings)?;

            if property_regex_set.is_match(property.name()) {
                // TODO(4601): We can keep track of the prefix string identifying
                // the "curr_node" and only insert from root if our iteration has
                // brought us to a new node higher up the hierarchy. Right now, we
                // insert from root for every new property.
                new_root.add(node_path, property.clone());
            }
        }

        Ok(new_root)
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
        let inspect_repo_data = {
            let locked_inspect_repo = self.inspect_repo.read().unwrap();
            locked_inspect_repo.fetch_data()
        };

        let mut pumped_data_tuple_results =
            join_all(inspect_repo_data.into_iter().map(move |inspect_data_packet| {
                async move {
                    let mut collector = InspectDataCollector::new();
                    match collector
                        .populate_data_map(&inspect_data_packet.component_out_proxy)
                        .await
                    {
                        Ok(_) => {
                            match Box::new(collector).take_data().and_then(|data_map| {
                                Some(data_map.into_iter().fold(Vec::new(), |mut acc, (_, data)| {
                                    match data {
                                        Data::Vmo(vmo) => match Snapshot::try_from(&vmo) {
                                            Ok(snapshot) => acc.push(snapshot),
                                            _ => {}
                                        },
                                        Data::Empty => {}
                                    }
                                    acc
                                }))
                            }) {
                                Some(snapshots) => {
                                    return Ok((
                                        inspect_data_packet.component_out_dir_path,
                                        snapshots,
                                        inspect_data_packet.component_node_selector,
                                        inspect_data_packet.node_property_selectors,
                                    ));
                                }
                                None => {
                                    return Err(format_err!(
                                        "Failed to parse snapshots for: {:?}.",
                                        inspect_data_packet.component_out_dir_path
                                    ));
                                }
                            };
                        }
                        Err(e) => return Err(e),
                    };
                }
            }))
            .await;

        // We drain the vector of pumped inspect data packets, consuming each inspect vmo
        // and filtering it using the provided selector regular expressions. Each filtered
        // inspect hierarchy is then added to an accumulator as a HierarchyData to be converted
        // into a JSON string and returned.
        let hierarchy_datas =
            pumped_data_tuple_results.drain(0..).fold(Vec::new(), |mut acc, pumped_data_tuple| {
                match pumped_data_tuple {
                    Ok((path, snapshots, selector_set, property_selectors)) => {
                        snapshots.into_iter().for_each(|snapshot| {
                            match ReaderServer::filter_inspect_snapshot(
                                snapshot,
                                &selector_set,
                                &property_selectors,
                            ) {
                                Ok(filtered_hierarchy) => {
                                    acc.push(HierarchyData {
                                        hierarchy: filtered_hierarchy,
                                        file_path: path
                                            .to_str()
                                            .expect("Can't have an invalid path here.")
                                            .to_string(),
                                        fields: vec![],
                                    });
                                }
                                // TODO(4601): Failing to parse a node hierarchy
                                // might be worth more than a silent failure.
                                Err(_) => {}
                            }
                        });
                        acc
                    }

                    // TODO(36761): What does it mean for IO to fail on a
                    // subset of directory data collections?
                    Err(_) => acc,
                }
            });

        let formatted_json_string = JsonFormatter::format(hierarchy_datas)?;

        let vmo_size: u64 = formatted_json_string.len() as u64;

        // TODO(lukenicholson): Inspect dumps may be large enough that they should be split
        // over multiple VMOs and streamed to the client
        let dump_vmo = zx::Vmo::create(vmo_size as u64)
            .map_err(|s| err_msg(format!("error creating buffer, zx status: {}", s)))?;

        dump_vmo
            .write(formatted_json_string.as_bytes(), 0)
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
        fuchsia_component::server::ServiceFs, fuchsia_inspect::Inspector, fuchsia_zircon as zx,
        fuchsia_zircon::Peered, futures::StreamExt,
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
        let inspector = Inspector::new();
        let root = inspector.root();

        let child_1 = root.create_child("child_1");
        let _tmp1 = child_1.create_int("some-int", 2);

        let child_1_1 = child_1.create_child("child_1_1");
        let _tmp2 = child_1_1.create_int("some-int", 3);
        let _tmp3 = child_1_1.create_int("not-wanted-int", 4);

        let child_2 = root.create_child("child_2");
        let _tmp2 = child_2.create_int("some-int", 2);

        let data = inspector.copy_vmo_data().unwrap();
        vmo.write(&data, 0).unwrap();
        fs.dir("objects").add_vmo_file_at("test.inspect", vmo, 0, 4096);

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
                let child_1_1_selector =
                    selectors::parse_selector(r#"**:root/child_1/**:some-int"#).unwrap();
                let child_2_selector = selectors::parse_selector(r#"**:root/child_2:*"#).unwrap();
                let mut inspect_repo = InspectDataRepository::new(vec![
                    Arc::new(child_1_1_selector),
                    Arc::new(child_2_selector),
                ]);

                let out_dir_proxy =
                    InspectDataCollector::find_directory_proxy(&path).await.unwrap();

                // The absolute moniker here is made up since the selector is a glob
                // selector, so any path would match.
                let absolute_moniker = vec!["a".to_string(), "b".to_string()];

                inspect_repo
                    .add("test.inspect".to_string(), absolute_moniker, path, out_dir_proxy)
                    .unwrap();

                let reader_server = ReaderServer::new(Arc::new(RwLock::new(inspect_repo)));

                let format_settings = FormatSettings {
                    format: Some(DisplaySettings::Text(TextSettings { indent: 4 })),
                };

                let inspect_data_dump = reader_server.format(format_settings).await.unwrap();
                let mut buf = vec![0; inspect_data_dump.size as usize];
                inspect_data_dump.vmo.read(&mut buf, 0).expect("reading vmo");

                let expected_result = "[
    {
        \"contents\": {
            \"root\": {
                \"child_1\": {
                    \"child_1_1\": {
                        \"some-int\": 3
                    }
                },
                \"child_2\": {
                    \"some-int\": 2
                }
            }
        },
        \"path\": \"/test-bindings2/out\"
    }
]";

                assert_eq!(std::str::from_utf8(&buf).unwrap(), expected_result);

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }
}
