// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

//! The collection module consists of methods and structures around two
//! different types of event streams: ComponentEvent and HubEvent.
//!
//! HubEvents are used internally for the HubCollector to attach watchers
//! for various different types in the Hub hierarchy. ComponentEvents are
//! exposed externally over a stream for use by the Archivist.

use {
    crate::{
        component_events::{ComponentEvent, ComponentEventData, Data, InspectReaderData},
        inspect,
    },
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    fuchsia_watch::{self, NodeType, PathEvent},
    futures::future::{join_all, BoxFuture},
    futures::{channel::mpsc, sink::SinkExt, stream::BoxStream, FutureExt, StreamExt},
    std::collections::HashMap,
    std::path::{Path, PathBuf, StripPrefixError},
};

/// This module supports watching the Fuchsia Component Hub structure for changes that are
/// converted into individual typed events.
///
/// Events on the different types of Hub directories are reported to a single stream, and the
/// handler for that stream is responsible for installing watchers for its nested types of
/// directories.
///
/// The Hub consists of the following types of directories:
/// - RealmInstance: Contains a Realm, consisting of a list of RealmInstanceDirectory (r/)
///                   and a list of ComponentInstanceDirectory (c/).
/// - RealmInstanceDirectory: Contains Realm instances, consisting of RealmInstance
///                           entries ("<numeric_id>/").
/// - ComponentInstanceDirectory: Contains ComponentInstance entries ("<numeric_id>/")
/// - ComponentInstance: May contain OutDirectory ("out/") hosted by the component.
///                      May contain ComponentListDirectory ("c/") if component is
///                      a runner.

/// The capacity for bounded channels used by this implementation.
static CHANNEL_CAPACITY: usize = 1024;

/// Ignore components with this name, since ComponentManager hub contains a cycle.
static COMPONENT_MANAGER_NAME: &str = "component_manager.cmx";

pub type DataMap = HashMap<String, Data>;

/// A realm path is a vector of realm names.
#[derive(Clone, Eq, PartialEq, Debug)]
pub struct RealmPath(pub Vec<String>);

impl RealmPath {
    fn as_string(&self) -> String {
        self.0.join("/").to_string()
    }
}

impl AsRef<Vec<String>> for RealmPath {
    fn as_ref(&self) -> &Vec<String> {
        &self.0
    }
}
impl AsMut<Vec<String>> for RealmPath {
    fn as_mut(&mut self) -> &mut Vec<String> {
        &mut self.0
    }
}

impl From<Vec<String>> for RealmPath {
    fn from(v: Vec<String>) -> Self {
        RealmPath(v)
    }
}

#[derive(Eq, PartialEq, Debug, Clone)]
enum PathParseState {
    RealmInstance,
    RealmListDirectory,
    ComponentInstance,
    ComponentListDirectory,
}

/// Determines whether a path identifies a specific component and if so returns corresonding
/// ComponentEventData.
pub fn path_to_event_data(path: impl AsRef<Path>) -> Result<ComponentEventData, Error> {
    let mut realm_path = RealmPath::from(vec![]);
    let mut current_component_name: Option<String> = None;
    let mut current_instance_id: Option<String> = None;
    let mut state = PathParseState::RealmInstance;

    let mut part_iter = path.as_ref().iter().map(|part| part.to_string_lossy().to_string());
    while let Some(part) = part_iter.next() {
        match &state {
            PathParseState::RealmInstance => {
                // Figure out if we are going into a realm list or component list.
                if part == "c" {
                    state = PathParseState::ComponentListDirectory;
                } else if part == "r" {
                    state = PathParseState::RealmListDirectory;
                } else {
                    return Err(format_err!(
                        "RealmInstance must contain 'c' or 'r', found {}",
                        part
                    ));
                }
            }
            PathParseState::RealmListDirectory => {
                // part is the realm name, check that we have a realm id as well
                part_iter.next().ok_or_else(|| format_err!("expected realm id, found None"))?;
                realm_path.as_mut().push(part);
                state = PathParseState::RealmInstance;
            }
            PathParseState::ComponentListDirectory => {
                current_component_name = Some(part);
                current_instance_id =
                    Some(part_iter.next().ok_or_else(|| {
                        format_err!("expected component instance id, found None")
                    })?);
                state = PathParseState::ComponentInstance;
            }
            PathParseState::ComponentInstance => {
                if part == "c" {
                    state = PathParseState::ComponentListDirectory;
                } else {
                    return Err(format_err!("expected None or 'c', found {}", part));
                }
            }
        }
    }

    if state == PathParseState::ComponentInstance
        && current_component_name.is_some()
        && current_instance_id.is_some()
    {
        Ok(ComponentEventData {
            realm_path,
            component_name: current_component_name.unwrap(),
            component_id: current_instance_id.unwrap(),
            component_data_map: None,
        })
    } else {
        Err(format_err!("process did not terminate at a component"))
    }
}

/// Stream of events on Components in the Hub.
pub type ComponentEventStream = BoxStream<'static, ComponentEvent>;

/// Channel type for sending ComponentEvents.
type ComponentEventChannel = mpsc::Sender<ComponentEvent>;

pub trait DataCollector {
    // Processes all previously collected data from the configured sources,
    // provides the returned DataMap with ownership of that data, returns the
    // map, and clears the collector state.
    //
    // If no data has yet been collected, or if the data had previously been
    // collected, then the return value will be None.
    fn take_data(self: Box<Self>) -> Option<DataMap>;

    // Triggers the process of collection, causing the collector to find and stage
    // all data it is configured to collect for transfer of ownership by the next
    // take_data call.
    fn collect(self: Box<Self>, path: PathBuf) -> BoxFuture<'static, Result<(), Error>>;
}

/// ExtraDataCollector is a composed data collector, combining multiple
/// data-collectors to retrieve [Data] from multiple sources.
struct ExtraDataCollector {
    data_collectors: Vec<Box<dyn DataCollector + Send>>,
}

impl DataCollector for ExtraDataCollector {
    /// Takes the contained extra data. Additions following this have no effect.
    fn take_data(mut self: Box<Self>) -> Option<DataMap> {
        let merged_map = self.data_collectors.drain(0..).fold(
            DataMap::new(),
            |mut accumulator, new_collector| {
                match new_collector.take_data() {
                    Some(new_data_map) => accumulator.extend(new_data_map),
                    None => (),
                }
                accumulator
            },
        );
        return Some(merged_map);
    }

    /// Collect extra data stored under the given path.
    ///
    /// Iterates over all data_collectors and applies collect onto them.
    fn collect(mut self: Box<Self>, path: PathBuf) -> BoxFuture<'static, Result<(), Error>> {
        async move {
            // TODO(lukenicholson): Can we process these async results to see if
            // any of the collections failed and return that failure?
            let results: Vec<BoxFuture<'static, Result<(), Error>>> = self
                .data_collectors
                .drain(0..)
                .map(|collector| collector.collect(path.clone()))
                .collect();

            let collection_succeeded = join_all(results)
                .map(|collection_results| {
                    collection_results.iter().fold(
                        true,
                        |all_collections_passed, collection_result| match collection_result {
                            Ok(_) => all_collections_passed && true,
                            _ => false,
                        },
                    )
                })
                .await;

            if collection_succeeded {
                return Ok(());
            } else {
                return Err(format_err!("Error collecting data."));
            }
        }
        .boxed()
    }
}

/// The HubCollector watches the component hub and passes all interesting events over its channels.
pub struct HubCollector {
    /// The path for the hub.
    path: PathBuf,

    /// A stream this HubCollector may pass to a client for it to listen to component events.
    component_event_receiver: Option<ComponentEventStream>,

    /// A channel passed to watchers for them to announce ComponentEvents.
    component_event_sender: ComponentEventChannel,

    /// Map of component paths to a vector of DataCollectors that have data about the
    /// component.
    component_extra_data: HashMap<PathBuf, Option<ExtraDataCollector>>,
}

struct ExistingPath(bool);

impl HubCollector {
    /// Create a new HubCollector watching the given path as the root of a hub realm.
    pub fn new(path: impl Into<PathBuf>) -> Result<Self, Error> {
        let (component_event_sender, component_event_receiver) = mpsc::channel(CHANNEL_CAPACITY);

        Ok(HubCollector {
            path: path.into(),
            component_event_receiver: Some(component_event_receiver.boxed()),
            component_event_sender,
            component_extra_data: HashMap::new(),
        })
    }

    /// Takes the component event stream for this collector.
    ///
    /// Returns a stream over which ComponentEvents are sent, only if no stream was previously taken.
    /// This method returns None on subsequent calls.
    pub fn component_events(&mut self) -> Option<ComponentEventStream> {
        self.component_event_receiver.take()
    }

    /// Adds a component to be watched.
    ///
    /// This publishes an event that the component has started or was existing.
    async fn add_component(
        &mut self,
        path: &Path,
        data: ComponentEventData,
        existing: ExistingPath,
    ) -> Result<(), Error> {
        self.component_extra_data.insert(path.to_path_buf(), None);
        match existing {
            ExistingPath(false) => {
                self.component_event_sender.send(ComponentEvent::Start(data)).await?
            }
            ExistingPath(true) => {
                self.component_event_sender.send(ComponentEvent::Existing(data)).await?
            }
        };
        Ok(())
    }

    /// Removes a watched component.
    ///
    /// This takes all extra data obtained by collectors and sends an event indicating that the
    /// component has stopped.
    async fn remove_component(
        &mut self,
        path: &Path,
        mut data: ComponentEventData,
    ) -> Result<(), Error> {
        data.component_data_map = match self.component_extra_data.remove(path) {
            Some(Some(data_collector)) => Box::new(data_collector).take_data(),
            _ => None,
        };

        self.component_event_sender.send(ComponentEvent::Stop(data)).await?;
        Ok(())
    }

    async fn add_out_watcher(
        &mut self,
        path: &Path,
        data: ComponentEventData,
    ) -> Result<(), Error> {
        // Clone the original collector to create a conceptual channel between the
        // producer of data and the consumer. This relies on the collector managing its
        // data with an ARC.
        let inspect_data_collector = Box::new(inspect::InspectDataCollector::new());
        let inspect_data_receiver = inspect_data_collector.clone();

        // NOTE: If you are adding a DataCollector to the ExtraDataCollector
        //       be aware that your DataCollector must have a shared data-source
        //       for which calling `collect` in one instance will result in updated
        //       data available in `take_data` of the other instance.
        let extra_data_collector =
            ExtraDataCollector { data_collectors: vec![inspect_data_collector] };

        let extra_data_receiver =
            ExtraDataCollector { data_collectors: vec![inspect_data_receiver] };

        let component_path = match path.parent() {
            Some(parent) => parent,
            None => {
                return Err(format_err!("Cannot process out directory with no parent."));
            }
        };

        // Store the receiving end of the extra data collectors in the
        // component map, to be retrieved by the archivist when the
        // component dies.
        self.component_extra_data
            .entry(component_path.to_path_buf())
            .and_modify(|v| *v = Some(extra_data_receiver));

        let inspect_out_dir_path = self.path.join(&path);
        let inspect_data_proxy =
            inspect::InspectDataCollector::find_directory_proxy(&inspect_out_dir_path).await?;

        let mut absolute_moniker = data.realm_path.0;
        absolute_moniker.push(data.component_name.clone());

        let inspect_reader_data = InspectReaderData {
            component_hierarchy_path: component_path.to_path_buf(),
            absolute_moniker: absolute_moniker,
            component_name: data.component_name,
            component_id: data.component_id,
            data_directory_proxy: Some(inspect_data_proxy),
        };

        self.component_event_sender
            .send(ComponentEvent::OutDirectoryAppeared(inspect_reader_data))
            .await?;

        // The incoming path is relative to the hub, we need to rejoin with the hub path to get an
        // absolute path.
        fasync::spawn(
            Box::new(extra_data_collector)
                .collect(self.path.join(path))
                .then(|_| futures::future::ready(())),
        );

        return Ok(());
    }

    fn check_if_out_directory(path: &Path) -> Option<ComponentEventData> {
        match path.file_name() {
            Some(n) if n != "out" => return None,
            _ => (),
        };

        if let Some(parent) = path.parent() {
            path_to_event_data(&parent).ok()
        } else {
            None
        }
    }

    /// Starts watching the hub.
    ///
    /// This method consumes the HubCollector.
    ///
    /// Returns a future that must be polled.
    pub async fn start(mut self) -> Result<(), Error> {
        let mut watch_stream = fuchsia_watch::watch_recursive(&self.path);

        let is_name_allowed =
            |component_name: &str| -> bool { component_name != COMPONENT_MANAGER_NAME };

        while let Some(result) = watch_stream.next().await {
            let event = match result {
                Err(_) => {
                    continue;
                }
                Ok(event) => event,
            };

            let relative_path = match event.as_ref().strip_prefix(&self.path) {
                Ok(p) => p,
                Err(StripPrefixError { .. }) => {
                    continue;
                }
            };

            match event {
                PathEvent::Added(_, NodeType::Directory) => {
                    if let Some(data) = HubCollector::check_if_out_directory(&relative_path) {
                        // TODO(41194): We don't need to worry about deadlocks or cycles if
                        // all inspect data is in a diagnostics-specific directory.
                        if is_name_allowed(&data.component_name) {
                            self.add_out_watcher(relative_path, data).await.unwrap_or_else(|e| {
                                eprintln!(
                                    "Error processing new out directory {}: {:?}",
                                    relative_path.display(),
                                    e
                                );
                            });
                        }
                    } else if let Ok(data) = path_to_event_data(&relative_path) {
                        self.add_component(relative_path, data, ExistingPath(false))
                            .await
                            .unwrap_or_else(|e| {
                                eprintln!("Error adding component: {:?}", e);
                            });
                    }
                }
                PathEvent::Existing(_, NodeType::Directory) => {
                    if let Some(data) = HubCollector::check_if_out_directory(&relative_path) {
                        if is_name_allowed(&data.component_name) {
                            self.add_out_watcher(relative_path, data).await.unwrap_or_else(|e| {
                                eprintln!(
                                    "Error processing existing out directory {}: {:?}",
                                    relative_path.display(),
                                    e
                                );
                            });
                        }
                    } else if let Ok(data) = path_to_event_data(&relative_path) {
                        self.add_component(relative_path, data, ExistingPath(true))
                            .await
                            .unwrap_or_else(|e| {
                                eprintln!("Error adding existing component: {:?}", e);
                            });
                    }
                }
                PathEvent::Removed(_) => {
                    if let Ok(data) = path_to_event_data(&relative_path) {
                        self.remove_component(relative_path, data).await.unwrap_or_else(|e| {
                            eprintln!("Error removing component: {:?}", e);
                        });
                    }
                }
                _ => {}
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fdio, fuchsia_component::server::ServiceFs, fuchsia_zircon as zx, fuchsia_zircon::Peered,
        std::fs,
    };

    macro_rules! make_component_event {
        ($fn_name:ident, $type:ident) => {
            paste::item! {
                fn [<make_ $fn_name>](
                    component_name: impl Into<String>,
                    component_id: impl Into<String>,
                    component_data_map: Option<DataMap>,
                ) -> ComponentEvent{
                    ComponentEvent::$type(ComponentEventData {
                        realm_path: vec![].into(),
                        component_name: component_name.into(),
                        component_id: component_id.into(),
                        component_data_map,
                    })
                }
                fn [<make_ $fn_name _with_realm>](
                    realm_path: impl Into<Vec<String>>,
                    component_name: impl Into<String>,
                    component_id: impl Into<String>,
                    component_data_map: Option<DataMap>,
                ) -> ComponentEvent{
                    ComponentEvent::$type(ComponentEventData {
                        realm_path: realm_path.into().into(),
                        component_name: component_name.into(),
                        component_id: component_id.into(),
                        component_data_map,
                    })
                }
            }
        };
    }

    make_component_event!(existing, Existing);
    make_component_event!(start, Start);
    make_component_event!(stop, Stop);

    fn make_out_directory_event(
        component_path: &str,
        component_name: &str,
        component_id: &str,
        absolute_moniker: Vec<String>,
    ) -> ComponentEvent {
        return ComponentEvent::OutDirectoryAppeared(InspectReaderData {
            component_hierarchy_path: component_path.to_string().into(),
            component_name: component_name.to_string(),
            absolute_moniker: absolute_moniker,
            component_id: component_id.to_string(),
            data_directory_proxy: None,
        });
    }

    #[derive(Debug)]
    enum DirectoryOperation {
        None,
        Create(PathBuf),
        Remove(PathBuf),
    }

    #[fasync::run_singlethreaded(test)]
    async fn extra_data_collector() {
        let path = PathBuf::from("/test-bindings");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        vmo.write(b"test", 0).unwrap();
        let vmo2 = zx::Vmo::create(4096).unwrap();
        vmo2.write(b"test", 0).unwrap();
        fs.dir("diagnostics").add_vmo_file_at("root.inspect", vmo, 0, 4096);
        fs.dir("diagnostics").add_vmo_file_at("root_not_inspect", vmo2, 0, 4096);

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
                let inspect_collector = Box::new(inspect::InspectDataCollector::new());
                let inspect_receiver = inspect_collector.clone();

                let extra_data_collector =
                    ExtraDataCollector { data_collectors: vec![inspect_collector] };

                let extra_data_receiver =
                    ExtraDataCollector { data_collectors: vec![inspect_receiver] };
                Box::new(extra_data_collector).collect(path).await.unwrap();

                let extra_data =
                    Box::new(extra_data_receiver).take_data().expect("collector missing data");

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
    async fn hub_collection() {
        let path = tempfile::tempdir().unwrap().into_path();

        let mut collector = HubCollector::new(path.clone()).unwrap();
        let mut component_events = collector.component_events().unwrap();

        fasync::spawn(collector.start().then(|_| futures::future::ready(())));

        fs::create_dir_all(path.join("c/my_component.cmx/10/out")).unwrap();

        assert_eq!(
            make_existing("my_component.cmx", "10", None),
            component_events.next().await.unwrap()
        );

        assert_eq!(
            make_out_directory_event(
                "c/my_component.cmx/10",
                "my_component.cmx",
                "10",
                vec!["my_component.cmx".to_string()]
            ),
            component_events.next().await.unwrap()
        );

        fs::create_dir_all(path.join("r/app/1/r/test/2/c/other_component.cmx/11/out")).unwrap();
        assert_eq!(
            make_start_with_realm(
                vec!["app".to_string(), "test".to_string()],
                "other_component.cmx",
                "11",
                None
            ),
            component_events.next().await.unwrap()
        );

        // TODO(37101): Why aren't out-directory events appearing here?

        fs::remove_dir_all(path.join("r")).unwrap();
        assert_eq!(
            make_stop_with_realm(
                vec!["app".to_string(), "test".to_string()],
                "other_component.cmx",
                "11",
                None
            ),
            component_events.next().await.unwrap()
        );

        fs::remove_dir_all(path.join("c")).unwrap();
        assert_eq!(
            make_stop("my_component.cmx", "10", Some(HashMap::new())),
            component_events.next().await.unwrap()
        );

        fs::create_dir_all(path.join("r/app/1/c/runner_component.cmx/12/out")).unwrap();
        assert_eq!(
            make_start_with_realm(vec!["app".to_string()], "runner_component.cmx", "12", None),
            component_events.next().await.unwrap()
        );

        assert_eq!(
            make_out_directory_event(
                "r/app/1/c/runner_component.cmx/12",
                "runner_component.cmx",
                "12",
                vec!["app".to_string(), "runner_component.cmx".to_string()],
            ),
            component_events.next().await.unwrap()
        );

        fs::create_dir_all(path.join("r/app/1/c/runner_component.cmx/12/c/with_runner.cmx/1"))
            .unwrap();

        assert_eq!(
            make_start_with_realm(vec!["app".to_string()], "with_runner.cmx", "1", None),
            component_events.next().await.unwrap()
        );

        fs::create_dir_all(path.join("r/app/1/c/archivist.cmx/12/out")).unwrap();

        // Test that the appearence of the archivist component is recorded but not
        // the appearence of its out directory.
        assert_eq!(
            make_start_with_realm(vec!["app".to_string()], "archivist.cmx", "12", None),
            component_events.next().await.unwrap()
        );

        assert_eq!(
            make_out_directory_event(
                "r/app/1/c/archivist.cmx/12",
                "archivist.cmx",
                "12",
                vec!["app".to_string(), "archivist.cmx".to_string()],
            ),
            component_events.next().await.unwrap()
        );

        fs::create_dir_all(path.join("r/app/1/c/component_manager.cmx/12/out")).unwrap();

        // Test that the appearence of the component_manager component is recorded but not
        // the appearence of its out directory.
        assert_eq!(
            make_start_with_realm(vec!["app".to_string()], "component_manager.cmx", "12", None),
            component_events.next().await.unwrap()
        );

        fs::remove_dir_all(&path).unwrap();
        assert_eq!(
            make_stop_with_realm(vec!["app".to_string()], "with_runner.cmx", "1", None),
            component_events.next().await.unwrap()
        );

        assert_eq!(
            make_stop_with_realm(
                vec!["app".to_string()],
                "runner_component.cmx",
                "12",
                Some(HashMap::new())
            ),
            component_events.next().await.unwrap()
        );
    }

    #[test]
    fn realm_path() {
        let realm_path = RealmPath::from(vec!["a".to_string(), "b".to_string(), "c".to_string()]);
        assert_eq!("a/b/c".to_string(), realm_path.as_string());
        let realm_path = RealmPath::from(vec!["a".to_string()]);
        assert_eq!("a".to_string(), realm_path.as_string());
        let realm_path = RealmPath::from(vec![]);
        assert_eq!("".to_string(), realm_path.as_string());
    }

    #[test]
    fn path_to_event_data_test() {
        let cases = vec![
            ("", None),
            ("c/", None),
            ("c/my_component.cmx", None),
            (
                "c/my_component.cmx/1",
                Some(ComponentEventData {
                    realm_path: vec![].into(),
                    component_name: "my_component.cmx".to_string(),
                    component_id: "1".to_string(),
                    component_data_map: None,
                }),
            ),
            ("c/my_component.cmx/1/out", None),
            ("c/my_component.cmx/1/out/diagnostics", None),
            ("c/my_component.cmx/1/out/diagnostics/root.inspect", None),
            ("c/my_component.cmx/1/c", None),
            ("c/my_component.cmx/1/c/running.cmx", None),
            (
                "c/my_component.cmx/1/c/running.cmx/2",
                Some(ComponentEventData {
                    realm_path: vec![].into(),
                    component_name: "running.cmx".to_string(),
                    component_id: "2".to_string(),
                    component_data_map: None,
                }),
            ),
            ("r", None),
            ("r/sys", None),
            ("r/sys/0", None),
            ("r/sys/0/c", None),
            ("r/sys/0/c/component.cmx", None),
            (
                "r/sys/0/c/component.cmx/1",
                Some(ComponentEventData {
                    realm_path: vec!["sys".to_string()].into(),
                    component_name: "component.cmx".to_string(),
                    component_id: "1".to_string(),
                    component_data_map: None,
                }),
            ),
            (
                "r/sys/0/c/component.cmx/1/c/run.cmx/2",
                Some(ComponentEventData {
                    realm_path: vec!["sys".to_string()].into(),
                    component_name: "run.cmx".to_string(),
                    component_id: "2".to_string(),
                    component_data_map: None,
                }),
            ),
            (
                "r/sys/0/r/a/1/r/b/2/r/c/3/c/temp.cmx/0",
                Some(ComponentEventData {
                    realm_path: vec![
                        "sys".to_string(),
                        "a".to_string(),
                        "b".to_string(),
                        "c".to_string(),
                    ]
                    .into(),
                    component_name: "temp.cmx".to_string(),
                    component_id: "0".to_string(),
                    component_data_map: None,
                }),
            ),
        ];

        for (path, expectation) in cases {
            match expectation {
                None => {
                    path_to_event_data(&path).unwrap_err();
                }
                Some(value) => {
                    assert_eq!(path_to_event_data(&path).unwrap(), value);
                }
            }
        }
    }
}
