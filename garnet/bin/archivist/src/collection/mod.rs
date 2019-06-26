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
    failure::{self, format_err, Error},
    fidl_fuchsia_io::NodeInfo,
    files_async, fuchsia_async as fasync,
    fuchsia_watch::{self, NodeType, PathEvent},
    fuchsia_zircon as zx,
    futures::{channel::mpsc, sink::SinkExt, stream::BoxStream, FutureExt, StreamExt},
    io_util,
    std::collections::HashMap,
    std::path::{Path, PathBuf},
    std::sync::{Arc, Mutex},
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
static OUT_DIRECTORY_POLL_MAX_SECONDS: i64 = 10;

/// Ignore components with this name, since reading our own output data may deadlock.
static ARCHIVIST_NAME: &str = "archivist.cmx";

/// Represents the data associated with a component event.
#[derive(Debug, Eq, PartialEq)]
pub struct ComponentEventData {
    /// The path to the component's realm.
    pub realm_path: RealmPath,

    /// The name of the component.
    pub component_name: String,

    /// The instance ID of the component.
    pub component_id: String,

    /// Extra data about this event (to be stored in extra files in the archive).
    pub extra_data: Option<ExtraDataMap>,
}

pub type ExtraDataMap = HashMap<String, ExtraData>;

/// Extra data associated with a component event.
///
/// This extra data is stored in the archive adjacent to the log file keyed by some name.
/// For example, components that exit may make an Inspect VMO available before they exit. A
/// reference to that data is maintains and written to disk along with ComponentEvent::Stop.
///
/// This may be extended to new types of data.
#[derive(Debug, Eq, PartialEq)]
pub enum ExtraData {
    /// Empty data, for testing.
    Empty,

    /// A VMO containing data associated with the event. The contents of the VMO should be written
    /// to disk.
    Vmo(zx::Vmo),
}

/// An event that occurred to a component.
#[derive(Debug, Eq, PartialEq)]
pub enum ComponentEvent {
    /// The component existed when the collection process started.
    Existing(ComponentEventData),

    /// We observed the component starting.
    Start(ComponentEventData),

    /// We observed the component stopping.
    Stop(ComponentEventData),
}

/// A realm path is a vector of realm names.
#[derive(Clone, Eq, PartialEq, Debug)]
pub struct RealmPath(Vec<String>);

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
            extra_data: None,
        })
    } else {
        Err(format_err!("process did not terminate at a component"))
    }
}

/// Stream of events on Components in the Hub.
pub type ComponentEventStream = BoxStream<'static, ComponentEvent>;

/// Channel type for sending ComponentEvents.
type ComponentEventChannel = mpsc::Sender<ComponentEvent>;

/// ExtraDataCollector holds any interesting information exposed by a component.
#[derive(Clone, Debug)]
struct ExtraDataCollector {
    /// The extra data.
    ///
    /// This is wrapped in an Arc Mutex so it can be shared between multiple data sources.
    /// The take_data method may be called once to retrieve the data.
    extra_data: Arc<Mutex<Option<ExtraDataMap>>>,
}

impl ExtraDataCollector {
    /// Construct a new ExtraDataCollector, wrapped by an Arc<Mutex>.
    fn new() -> Self {
        ExtraDataCollector { extra_data: Arc::new(Mutex::new(Some(ExtraDataMap::new()))) }
    }

    /// Takes the contained extra data. Additions following this have no effect.
    fn take_data(self) -> Option<ExtraDataMap> {
        self.extra_data.lock().unwrap().take()
    }

    /// Adds a key value to the contained vector if it hasn't been taken yet. Otherwise, does
    /// nothing.
    fn maybe_add(&mut self, key: String, value: ExtraData) {
        let mut data = self.extra_data.lock().unwrap();
        match data.as_mut() {
            Some(map) => {
                map.insert(key, value);
            }
            _ => {}
        };
    }

    /// Collect extra data stored under the given path.
    ///
    /// This currently only does a single pass over the directory to find information.
    async fn collect(mut self, path: PathBuf) -> Result<(), Error> {
        let proxy = match io_util::open_directory_in_namespace(
            &path.to_string_lossy(),
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        ) {
            Ok(proxy) => proxy,
            Err(e) => {
                return Err(format_err!("Failed to open out directory at {:?}: {}", path, e));
            }
        };

        for entry in await!(files_async::readdir_recursive(proxy))?.into_iter() {
            // We are only currently interested in inspect files.
            if !entry.name.ends_with(".inspect") || entry.dir_type != files_async::DirentType::File
            {
                continue;
            }

            let path = path.join(entry.name);
            let proxy = match io_util::open_file_in_namespace(
                &path.to_string_lossy(),
                io_util::OPEN_RIGHT_READABLE,
            ) {
                Ok(proxy) => proxy,
                Err(_) => {
                    continue;
                }
            };

            // Obtain the vmo backing any VmoFiles.
            match await!(proxy.describe()) {
                Ok(nodeinfo) => match nodeinfo {
                    NodeInfo::Vmofile(vmofile) => {
                        self.maybe_add(
                            path.file_name().unwrap().to_string_lossy().to_string(),
                            ExtraData::Vmo(vmofile.vmo),
                        );
                    }
                    _ => {}
                },
                Err(_) => {}
            }
        }

        Ok(())
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

    /// Map of component paths to extra data that may have been collected for that component.
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
    async fn add_component<'a>(
        &'a mut self,
        path: PathBuf,
        data: ComponentEventData,
        existing: ExistingPath,
    ) -> Result<(), Error> {
        self.component_extra_data.insert(path, None);
        match existing {
            ExistingPath(false) => {
                await!(self.component_event_sender.send(ComponentEvent::Start(data)))?
            }
            ExistingPath(true) => {
                await!(self.component_event_sender.send(ComponentEvent::Existing(data)))?
            }
        };
        Ok(())
    }

    /// Removes a watched component.
    ///
    /// This takes all extra data obtained by collectors and sends an event indicating that the
    /// component has stopped.
    async fn remove_component<'a>(
        &'a mut self,
        path: PathBuf,
        mut data: ComponentEventData,
    ) -> Result<(), Error> {
        data.extra_data = match self.component_extra_data.remove(&path) {
            Some(Some(data_ptr)) => data_ptr.take_data(),
            _ => None,
        };

        await!(self.component_event_sender.send(ComponentEvent::Stop(data)))?;
        Ok(())
    }

    fn add_out_watcher<'a>(&'a mut self, path: PathBuf) {
        let collector = ExtraDataCollector::new();

        let collector_clone = collector.clone();
        let component_path = match path.parent() {
            Some(parent) => parent,
            None => {
                return;
            }
        };
        self.component_extra_data
            .entry(component_path.to_path_buf())
            .and_modify(move |v| *v = Some(collector_clone));

        // The incoming path is relative to the hub, we need to rejoin with the hub path to get an
        // absolute path.
        fasync::spawn(collector.collect(self.path.join(path)).then(async move |_| ()));
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

        while let Some(result) = await!(watch_stream.next()) {
            let event = match result {
                Err(_) => {
                    continue;
                }
                Ok(event) => event,
            };

            let relative_path = match event.as_ref().strip_prefix(&self.path) {
                Ok(p) => p.to_path_buf(),
                _ => {
                    continue;
                }
            };

            match event {
                PathEvent::Added(_, NodeType::Directory) => {
                    if let Some(data) = HubCollector::check_if_out_directory(&relative_path) {
                        if data.component_name != ARCHIVIST_NAME {
                            self.add_out_watcher(relative_path);
                        }
                    } else if let Ok(data) = path_to_event_data(&relative_path) {
                        await!(self.add_component(relative_path, data, ExistingPath(false)))
                            .unwrap_or_else(|e| {
                                eprintln!("Error adding component: {:?}", e);
                            });
                    }
                }
                PathEvent::Existing(_, NodeType::Directory) => {
                    if let Some(data) = HubCollector::check_if_out_directory(&relative_path) {
                        if data.component_name != ARCHIVIST_NAME {
                            self.add_out_watcher(relative_path);
                        }
                    } else if let Ok(data) = path_to_event_data(&relative_path) {
                        await!(self.add_component(relative_path, data, ExistingPath(true)))
                            .unwrap_or_else(|e| {
                                eprintln!("Error adding existing component: {:?}", e);
                            });
                    }
                }
                PathEvent::Removed(_) => {
                    if let Ok(data) = path_to_event_data(&relative_path) {
                        await!(self.remove_component(relative_path, data)).unwrap_or_else(|e| {
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
    use {fdio, fuchsia_component::server::ServiceFs, fuchsia_zircon::Peered, std::fs};

    macro_rules! make_component_event {
        ($fn_name:ident, $type:ident) => {
            paste::item! {
                fn [<make_ $fn_name>](
                    component_name: impl Into<String>,
                    component_id: impl Into<String>,
                    extra_data: Option<ExtraDataMap>,
                ) -> ComponentEvent{
                    ComponentEvent::$type(ComponentEventData {
                        realm_path: vec![].into(),
                        component_name: component_name.into(),
                        component_id: component_id.into(),
                        extra_data,
                    })
                }
                fn [<make_ $fn_name _with_realm>](
                    realm_path: impl Into<Vec<String>>,
                    component_name: impl Into<String>,
                    component_id: impl Into<String>,
                    extra_data: Option<ExtraDataMap>,
                ) -> ComponentEvent{
                    ComponentEvent::$type(ComponentEventData {
                        realm_path: realm_path.into().into(),
                        component_name: component_name.into(),
                        component_id: component_id.into(),
                        extra_data,
                    })
                }
            }
        };
    }

    make_component_event!(existing, Existing);
    make_component_event!(start, Start);
    make_component_event!(stop, Stop);

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
                let collector = ExtraDataCollector::new();

                await!(collector.clone().collect(path)).unwrap();

                let extra_data = collector.take_data().expect("collector missing data");
                assert_eq!(1, extra_data.len());

                let extra = extra_data.get("root.inspect");
                assert!(extra.is_some());

                match extra.unwrap() {
                    ExtraData::Vmo(vmo) => {
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

        await!(fasync::OnSignals::new(&done0, zx::Signals::USER_0)).unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn hub_collection() {
        let path = tempfile::tempdir().unwrap().into_path();

        let mut collector = HubCollector::new(path.clone()).unwrap();
        let mut component_events = collector.component_events().unwrap();

        fasync::spawn(collector.start().then(async move |_| ()));

        fs::create_dir_all(path.join("c/my_component.cmx/10/out")).unwrap();

        assert_eq!(
            make_existing("my_component.cmx", "10", None),
            await!(component_events.next()).unwrap()
        );

        fs::create_dir_all(path.join("r/app/1/r/test/2/c/other_component.cmx/11/out")).unwrap();
        assert_eq!(
            make_start_with_realm(
                vec!["app".to_string(), "test".to_string()],
                "other_component.cmx",
                "11",
                None
            ),
            await!(component_events.next()).unwrap()
        );

        fs::remove_dir_all(path.join("r")).unwrap();
        assert_eq!(
            make_stop_with_realm(
                vec!["app".to_string(), "test".to_string()],
                "other_component.cmx",
                "11",
                None
            ),
            await!(component_events.next()).unwrap()
        );

        fs::remove_dir_all(path.join("c")).unwrap();
        assert_eq!(
            make_stop("my_component.cmx", "10", Some(HashMap::new())),
            await!(component_events.next()).unwrap()
        );

        fs::create_dir_all(path.join("r/app/1/c/runner_component.cmx/12/out")).unwrap();
        assert_eq!(
            make_start_with_realm(vec!["app".to_string()], "runner_component.cmx", "12", None),
            await!(component_events.next()).unwrap()
        );

        fs::create_dir_all(path.join("r/app/1/c/runner_component.cmx/12/c/with_runner.cmx/1"))
            .unwrap();
        assert_eq!(
            make_start_with_realm(vec!["app".to_string()], "with_runner.cmx", "1", None),
            await!(component_events.next()).unwrap()
        );

        fs::remove_dir_all(&path).unwrap();
        assert_eq!(
            make_stop_with_realm(vec!["app".to_string()], "with_runner.cmx", "1", None),
            await!(component_events.next()).unwrap()
        );
        assert_eq!(
            make_stop_with_realm(
                vec!["app".to_string()],
                "runner_component.cmx",
                "12",
                Some(HashMap::new())
            ),
            await!(component_events.next()).unwrap()
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
                    extra_data: None,
                }),
            ),
            ("c/my_component.cmx/1/out", None),
            ("c/my_component.cmx/1/out/objects", None),
            ("c/my_component.cmx/1/out/objects/root.inspect", None),
            ("c/my_component.cmx/1/c", None),
            ("c/my_component.cmx/1/c/running.cmx", None),
            (
                "c/my_component.cmx/1/c/running.cmx/2",
                Some(ComponentEventData {
                    realm_path: vec![].into(),
                    component_name: "running.cmx".to_string(),
                    component_id: "2".to_string(),
                    extra_data: None,
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
                    extra_data: None,
                }),
            ),
            (
                "r/sys/0/c/component.cmx/1/c/run.cmx/2",
                Some(ComponentEventData {
                    realm_path: vec!["sys".to_string()].into(),
                    component_name: "run.cmx".to_string(),
                    component_id: "2".to_string(),
                    extra_data: None,
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
                    extra_data: None,
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
