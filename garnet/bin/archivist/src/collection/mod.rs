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
    fuchsia_vfs_watcher::Watcher,
    fuchsia_zircon::{self as zx, DurationNum},
    futures::{
        channel::mpsc, sink::SinkExt, stream::BoxStream, FutureExt, StreamExt, TryFutureExt,
        TryStreamExt,
    },
    io_util,
    std::collections::HashMap,
    std::fs::File,
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

/// An event on a particular path on the file system.
#[derive(Clone, Debug, Eq, PartialEq)]
enum PathEvent {
    /// A watcher observed the path being added.
    Added(PathBuf),

    /// A watcher observed the path existing when it started.
    Existing(PathBuf),

    /// A watcher observed the path being removed.
    Removed(PathBuf),
}

impl PathEvent {
    /// Prefix the contained path with a given prefix.
    fn with_path_prefix(self, path: &Path) -> Self {
        match self {
            PathEvent::Added(filename) => PathEvent::Added(path.join(filename)),
            PathEvent::Existing(filename) => PathEvent::Existing(path.join(filename)),
            PathEvent::Removed(filename) => PathEvent::Removed(path.join(filename)),
        }
    }
}

/// PathEvents are convertable to their wrapped path.
impl AsRef<Path> for PathEvent {
    fn as_ref(&self) -> &Path {
        match self {
            PathEvent::Added(filename) => &filename,
            PathEvent::Existing(filename) => &filename,
            PathEvent::Removed(filename) => &filename,
        }
    }
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

/// Defines context for entities in the Hub.
///
/// Each HubContext is associated with some entities that share a realm path. HubContext keeps
/// track of whether all ancestors of the associated entities were present when the Hub was
/// originally observed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HubContext {
    /// The realm associated with the entity this is for.
    realm_path: RealmPath,

    /// True only if the associated entity and all of its ancestors existed when collection
    /// operations started. Keeping track of this is necessary to tell that an "existing" file B
    /// inside of a directory A is actually a "new" file in the hub if A was a "new" directory in
    /// the hub.
    was_existing: bool,
}

impl HubContext {
    /// Creates a new HubContext for some root realm.
    fn new() -> Self {
        HubContext { realm_path: vec![].into(), was_existing: true }
    }

    /// Creates a new HubContext for an entity that was observed to be created in the same realm as
    /// the entity this HubContext is for.
    fn not_existing(&self) -> Self {
        let mut ret = self.clone();
        ret.was_existing = false;
        ret
    }

    /// Creates a new HubContext for a nested realm that was observed to be created.
    fn join_created(&self, name: impl Into<String>) -> Self {
        let mut ret = self.join(name);
        ret.was_existing = false;
        ret
    }

    /// Creates a new HubContext for a nested realm that was existing when the parent realm was
    /// entered.
    fn join(&self, name: impl Into<String>) -> Self {
        let mut ret = self.clone();
        ret.realm_path.as_mut().push(name.into());
        ret
    }

    /// Get the next context based on the path event.
    ///
    /// If the path was added, the next context will not have |was_existing| set.
    /// If the path was removed, there is no next context.
    fn next(&self, event: &PathEvent) -> Option<Self> {
        match event {
            PathEvent::Removed(_) => None,
            PathEvent::Added(_) => Some(self.not_existing()),
            PathEvent::Existing(_) => Some(self.clone()),
        }
    }

    /// Get the next context based on the path event for a new realm.
    ///
    /// If the path was added, the next context will not have |was_existing| set.
    /// If the path was removed, there is no next context.
    fn next_realm(&self, event: &PathEvent) -> Option<Self> {
        match event {
            PathEvent::Removed(_) => None,
            PathEvent::Added(path) => match path.file_name() {
                Some(name) => Some(self.join_created(name.to_string_lossy())),
                None => None,
            },
            PathEvent::Existing(path) => match path.file_name() {
                Some(name) => Some(self.join(name.to_string_lossy())),
                None => None,
            },
        }
    }
}

/// Data for an event occurring on the Hub.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HubEventData {
    /// The context for the event, including the realm the event occurred in.
    hub_context: HubContext,

    /// The path event for some path on the hub.
    path_event: PathEvent,
}

/// Defines an individual event occurring in the Hub. Receivers for this event may create new
/// watchers on the event paths.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum HubEvent {
    /// A new directory for a component was found. This may contain "out" for the component, or
    /// it may contain "c" if the component is a runner.
    ComponentInstance(Option<HubEventData>),

    /// A new directory containing ComponentInstance was found.
    ComponentInstanceDirectory(Option<HubEventData>),

    /// A new directory containing RealmInstance was found.
    RealmInstanceDirectory(Option<HubEventData>),

    /// A new directory containing RealmListDirectory and/or ComponentListDirectory was found.
    RealmInstance(Option<HubEventData>),
}

impl HubEvent {
    /// Replaces the contained data with the given data.
    fn with_data(&self, data: HubEventData) -> Self {
        match self {
            HubEvent::ComponentInstance(_) => HubEvent::ComponentInstance(Some(data)),
            HubEvent::ComponentInstanceDirectory(_) => {
                HubEvent::ComponentInstanceDirectory(Some(data))
            }
            HubEvent::RealmInstanceDirectory(_) => HubEvent::RealmInstanceDirectory(Some(data)),
            HubEvent::RealmInstance(_) => HubEvent::RealmInstance(Some(data)),
        }
    }
}

/// Stream of events on Components in the Hub.
pub type ComponentEventStream = BoxStream<'static, ComponentEvent>;

/// Stream of events on Hub directory paths.
pub type HubEventStream = BoxStream<'static, HubEvent>;

/// Channel type for sending ComponentEvents.
type ComponentEventChannel = mpsc::Sender<ComponentEvent>;

/// Channel type for sending HubEvents.
type HubEventChannel = mpsc::Sender<HubEvent>;

/// Stream of events on a particular path in the Hub.
type PathStream = BoxStream<'static, PathEvent>;

/// Watches the given path for file changes.
///
/// Returns a stream of PathEvents if a watcher could be installed on the path successfully.
fn watch_path(path: &Path) -> Result<PathStream, Error> {
    let mut watcher = Watcher::new(&File::open(path)?)?;

    let (mut tx, rx) = mpsc::channel(0);
    let path_future = async move {
        while let Ok(message) = await!(watcher.try_next()) {
            if message.is_none() {
                break;
            }

            let message = message.unwrap();

            if message.filename.as_os_str() == "." {
                continue;
            }
            let value = match message.event {
                fuchsia_vfs_watcher::WatchEvent::EXISTING => PathEvent::Existing(message.filename),
                fuchsia_vfs_watcher::WatchEvent::ADD_FILE => PathEvent::Added(message.filename),
                fuchsia_vfs_watcher::WatchEvent::REMOVE_FILE => {
                    PathEvent::Removed(message.filename)
                }
                _ => {
                    continue;
                }
            };

            await!(tx.send(value)).unwrap();
        }
    };

    fasync::spawn(path_future);

    Ok(rx.boxed())
}

/// Watcher for a specific path on the hub.
///
/// This struct generalizes watching a hub location for events of a specific type. All files
/// observed being added or removed at the wrapped path will be send on a HubEventChannel with the
/// wrapped context.
struct HubDirectoryWatcher {
    path: PathBuf,
    context: HubContext,
    path_stream: PathStream,
}

impl HubDirectoryWatcher {
    /// Create a new watcher for the given path with the given context.
    fn new(path: impl Into<PathBuf>, context: HubContext) -> Result<Self, Error> {
        let path: PathBuf = path.into();
        let path_stream = watch_path(&path)?;
        Ok(HubDirectoryWatcher { path, context, path_stream })
    }

    /// Process this watcher into events that are stored in a channel.
    /// The given hub_event specifies the type of events passed from this watcher.
    async fn process(
        mut self,
        mut channel: HubEventChannel,
        hub_event: HubEvent,
    ) -> Result<(), Error> {
        while let Some(path_event) = await!(self.path_stream.next()) {
            await!(channel.send(hub_event.with_data(HubEventData {
                hub_context: self.context.clone(),
                path_event: path_event.with_path_prefix(&self.path)
            })))?;
        }

        Ok(())
    }
}

/// Convenience function to open a path as a DirectoryProxy.
/// Parses a path into component_name and component_id.
fn parse_component_path(path: &Path) -> Result<(String, String), Error> {
    let component_id = path.file_name().ok_or(format_err!("No file name"))?;
    let component_name = path
        .parent()
        .ok_or(format_err!("No parent"))?
        .file_name()
        .ok_or(format_err!("No parent name"))?;
    Ok((component_name.to_string_lossy().to_string(), component_id.to_string_lossy().to_string()))
}

/// ExtraDataCollector holds any interesting information exposed by a component.
#[derive(Clone)]
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

    /// Watch the given path, storing interesting information that is found into the output.
    async fn watch(mut self, path: PathBuf) -> Result<(), Error> {
        let out_path = path.join("out");

        // Some out directory implementations do not support watching for changes, so we poll
        // waiting for the out directory to appear using exponential backoff.
        // TODO(crjohns): Add a signal to wait on without polling.
        let mut sleep_time = 10_i64.millis();
        while path.exists() && !out_path.exists() {
            await!(fasync::Timer::new(sleep_time.after_now()));
            sleep_time = sleep_time * 2;
            if sleep_time > OUT_DIRECTORY_POLL_MAX_SECONDS.seconds() {
                sleep_time = OUT_DIRECTORY_POLL_MAX_SECONDS.seconds();
            }
        }

        if !out_path.exists() {
            return Err(format_err!("No out directory was mounted at {:?}", path));
        }

        let proxy = match io_util::open_directory_in_namespace(&out_path.to_string_lossy()) {
            Ok(proxy) => proxy,
            Err(e) => {
                return Err(format_err!("Failed to open out directory at {:?}: {}", out_path, e));
            }
        };

        for entry in await!(files_async::readdir_recursive(proxy))?.into_iter() {
            // We are only currently interested in inspect files.
            if !entry.name.ends_with(".inspect") || entry.dir_type != files_async::DirentType::File
            {
                continue;
            }

            let path = out_path.join(entry.name);
            let proxy = match io_util::open_file_in_namespace(&path.to_string_lossy()) {
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

/// Wraps the parameters for the next watcher that needs to be attached to the hub.
#[derive(Debug, PartialEq, Eq)]
struct NextHubWatcherParams {
    /// The next path to watch.
    path: PathBuf,

    /// The context for the next watcher.
    hub_context: HubContext,

    /// The type of hub event to build in the next watcher.
    hub_event: HubEvent,
}

/// The HubCollector watches the component hub and passes all interesting events over its channels.
pub struct HubCollector {
    /// The path for the hub.
    path: PathBuf,

    /// The stream on which this HubCollector receives events on entities being added and removed
    /// from the hub.
    hub_event_receiver: HubEventStream,

    /// A stream this HubCollector may pass to a client for it to listen to component events.
    component_event_receiver: Option<ComponentEventStream>,

    /// A channel passed to watchers for them to announce HubEvents.
    hub_event_sender: HubEventChannel,

    /// A channel passed to watchers for them to announce ComponentEvents.
    component_event_sender: ComponentEventChannel,

    /// Optional channel for this HubCollector to forward HubEvents to a client.
    public_hub_event_sender: Option<HubEventChannel>,

    /// Map of component paths to extra data that may have been collected for that component.
    component_extra_data: HashMap<PathBuf, ExtraDataCollector>,
}

impl HubCollector {
    /// Create a new HubCollector watching the given path as the root of a hub realm.
    pub fn new(path: impl Into<PathBuf>) -> Result<Self, Error> {
        let (hub_event_sender, hub_event_receiver) = mpsc::channel(CHANNEL_CAPACITY);
        let (component_event_sender, component_event_receiver) = mpsc::channel(CHANNEL_CAPACITY);

        Ok(HubCollector {
            path: path.into(),
            hub_event_receiver: hub_event_receiver.boxed(),
            component_event_receiver: Some(component_event_receiver.boxed()),
            hub_event_sender,
            component_event_sender,
            public_hub_event_sender: None,
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

    /// Create and take a hub event stream for this collector.
    ///
    /// Returns a stream over which HubEvents are sent, only if no stream was previously taken.
    /// This method returns None on subsequent calls.
    pub fn hub_events(&mut self) -> Option<HubEventStream> {
        match self.public_hub_event_sender {
            None => {
                let (tx, rx) = mpsc::channel(CHANNEL_CAPACITY);
                self.public_hub_event_sender = Some(tx);
                Some(rx.boxed())
            }
            _ => None,
        }
    }

    /// Adds a component to be watched.
    ///
    /// This publishes an event that the component has started or was existing, and it attaches a
    /// watcher for collecting extra data about the component.
    async fn add_component<'a>(
        &'a mut self,
        path: impl Into<PathBuf>,
        context: &'a HubContext,
        existing: bool,
    ) -> Result<(), Error> {
        let path: PathBuf = path.into();
        let (component_name, component_id) = parse_component_path(&path)?;

        let entry_ref = self
            .component_extra_data
            .entry(path.clone())
            .or_insert_with(|| ExtraDataCollector::new());

        fasync::spawn(entry_ref.clone().watch(path).then(async move |_| ()));

        match existing {
            false => await!(self.component_event_sender.send(ComponentEvent::Start(
                ComponentEventData {
                    realm_path: context.realm_path.clone(),
                    component_name,
                    component_id,
                    extra_data: None,
                }
            )))?,
            true => await!(self.component_event_sender.send(ComponentEvent::Existing(
                ComponentEventData {
                    realm_path: context.realm_path.clone(),
                    component_name,
                    component_id,
                    extra_data: None,
                }
            )))?,
        };
        Ok(())
    }

    /// Removes a watched component.
    ///
    /// This takes all extra data obtained by collectors and sends an event indicating that the
    /// component has stopped.
    async fn remove_component<'a>(
        &'a mut self,
        path: &'a Path,
        context: &'a HubContext,
    ) -> Result<(), Error> {
        let (component_name, component_id) = parse_component_path(&path)?;

        let extra_data = match self.component_extra_data.remove(path) {
            Some(data_ptr) => data_ptr.take_data(),
            None => None,
        };

        await!(self.component_event_sender.send(ComponentEvent::Stop(ComponentEventData {
            realm_path: context.realm_path.clone(),
            component_name,
            component_id,
            extra_data,
        })))?;
        Ok(())
    }

    /// Starts watching the hub.
    ///
    /// This method consumes the HubCollector.
    ///
    /// Returns a future that must be polled.
    pub async fn start(mut self) -> Result<(), Error> {
        await!(self.hub_event_sender.send(HubEvent::RealmInstance(Some(HubEventData {
            hub_context: HubContext::new(),
            path_event: PathEvent::Existing(self.path.clone()),
        }))))
        .expect("send initial hub event");

        while let Some(event) = await!(self.hub_event_receiver.next()) {
            let next_watchers = next_watchers_for_event(&event);

            if let Some(public_hub_event_sender) = self.public_hub_event_sender.as_mut() {
                await!(public_hub_event_sender.send(event.clone())).unwrap();
            }

            match event {
                HubEvent::ComponentInstance(Some(data)) => match &data.path_event {
                    PathEvent::Added(path) | PathEvent::Existing(path) => {
                        let existing = data.hub_context.was_existing
                            && match &data.path_event {
                                PathEvent::Existing(_) => true,
                                _ => false,
                            };
                        await!(self.add_component(path, &data.hub_context, existing).or_else(
                            async move |e| -> Result<(), Error> {
                                eprintln!("Error adding component: {:?}", e);
                                Ok(())
                            },
                        ))
                        .unwrap();
                    }
                    PathEvent::Removed(path) => {
                        await!(self.remove_component(&path, &data.hub_context).or_else(
                            async move |e| -> Result<(), Error> {
                                eprintln!("Error removing component: {:?}", e);
                                Ok(())
                            },
                        ))
                        .unwrap();
                    }
                },
                _ => (),
            };

            for next_watcher in next_watchers.into_iter() {
                // We use an asynchronous watcher for each next directory to be watched. Otherwise,
                // there is a potential race condition where we need to watch for a directory that
                // isn't yet available. For instance: watching "r/" and "c/" in a RealmInstance
                // when they are not available yet.
                let sender = self.hub_event_sender.clone();
                let next_watcher_future = async move {
                    let parent_path = next_watcher.path.parent().unwrap();
                    let expected_filename = next_watcher.path.file_name().unwrap();
                    let mut stream = match watch_path(parent_path) {
                        Ok(stream) => stream,
                        _ => {
                            return;
                        }
                    };

                    while let Some(path_event) = await!(stream.next()) {
                        // Found the directory we were looking for.
                        if expected_filename == path_event.as_ref().as_os_str() {
                            let context = match path_event {
                                PathEvent::Added(_) => next_watcher.hub_context.not_existing(),
                                PathEvent::Existing(_) => next_watcher.hub_context.clone(),
                                PathEvent::Removed(_) => {
                                    continue;
                                }
                            };
                            let watcher = match HubDirectoryWatcher::new(
                                parent_path.join(path_event),
                                context,
                            ) {
                                Ok(w) => w,
                                Err(e) => {
                                    eprintln!("Error watching hub directory: {:?}", e);
                                    break;
                                }
                            };
                            fasync::spawn(
                                watcher
                                    .process(sender.clone(), next_watcher.hub_event.clone())
                                    .then(async move |_| ()),
                            );
                        }
                    }
                };
                fasync::spawn(next_watcher_future);
            }
        }
        Ok(())
    }
}

/// Generates a vector of the next hub locations that need to be watched after receiving the given
/// event.
fn next_watchers_for_event(event: &HubEvent) -> Vec<NextHubWatcherParams> {
    match event {
        HubEvent::RealmInstance(Some(data)) => match data.hub_context.next(&data.path_event) {
            Some(context) => vec![
                NextHubWatcherParams {
                    path: data.path_event.as_ref().join("r"),
                    hub_context: context.clone(),
                    hub_event: HubEvent::RealmInstanceDirectory(None),
                },
                NextHubWatcherParams {
                    path: data.path_event.as_ref().join("c"),
                    hub_context: context,
                    hub_event: HubEvent::ComponentInstanceDirectory(None),
                },
            ],
            _ => vec![],
        },
        HubEvent::RealmInstanceDirectory(Some(data)) => {
            match data.hub_context.next_realm(&data.path_event) {
                Some(context) => vec![NextHubWatcherParams {
                    path: data.path_event.as_ref().to_path_buf(),
                    hub_context: context,
                    hub_event: HubEvent::RealmInstance(None),
                }],
                _ => vec![],
            }
        }
        HubEvent::ComponentInstanceDirectory(Some(data)) => {
            match data.hub_context.next(&data.path_event) {
                Some(context) => vec![NextHubWatcherParams {
                    path: data.path_event.as_ref().to_path_buf(),
                    hub_context: context,
                    hub_event: HubEvent::ComponentInstance(None),
                }],
                _ => vec![],
            }
        }
        HubEvent::ComponentInstance(Some(data)) => match data.hub_context.next(&data.path_event) {
            // Components may be runners that hold components themselves. Make sure we look for
            // such a directory.
            Some(context) => vec![NextHubWatcherParams {
                path: data.path_event.as_ref().join("c"),
                hub_context: context,
                hub_event: HubEvent::ComponentInstanceDirectory(None),
            }],
            _ => vec![],
        },
        _ => vec![],
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

    macro_rules! make_hub_event {
        ($fn_name:ident, $hub_event:ident, $path_event:ident) => {
            paste::item! {
                fn [<make_ $fn_name>](
                    realm_path: RealmPath,
                    path: impl Into<PathBuf>
                ) -> HubEvent {
                    let mut ctx = HubContext::new();
                    ctx.realm_path = realm_path;
                    HubEvent::$hub_event(Some(HubEventData{
                        hub_context: ctx,
                        path_event: PathEvent::$path_event(path.into()),
                    }))
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

    macro_rules! make_hub_directory_watcher_test {
        ($fn_name:ident, $type_lower:ident, $type_cap:ident) => {
            paste::item! {
            make_hub_event!([<$type_lower _start>], $type_cap, Added);
            make_hub_event!([<$type_lower _existing>], $type_cap, Existing);
            make_hub_event!([<$type_lower _stop>], $type_cap, Removed);

            #[fasync::run_until_stalled(test)]
                async fn $fn_name () {
                    let dir = tempfile::tempdir().unwrap();
                    let path = dir.into_path();

                    let (tx, mut rx) = mpsc::channel(0);

                    fs::create_dir(path.join("existing")).unwrap();
                    let watch = HubDirectoryWatcher::new(&path, HubContext::new())
                        .unwrap()
                        .process(tx, HubEvent::$type_cap(None));
                    fasync::spawn_local(watch.then(async move |_| ()));

                    let test_cases = vec![
                        (
                            DirectoryOperation::None,
                            vec![[<make_ $type_lower _existing>](vec![].into(), path.join("existing"))],
                        ),
                        (
                            DirectoryOperation::Create(path.join("1")),
                            vec![[<make_ $type_lower _start>](vec![].into(), path.join("1"))],
                        ),
                        (
                            DirectoryOperation::Create(path.join("2")),
                            vec![[<make_ $type_lower _start>](vec![].into(), path.join("2"))],
                        ),
                        (
                            DirectoryOperation::Remove(path.join("2")),
                            vec![[<make_ $type_lower _stop>](vec![].into(), path.join("2"))],
                        ),
                        (
                            DirectoryOperation::Remove(path.clone()),
                            vec![
                                [<make_ $type_lower _stop>](vec![].into(), path.join("existing")),
                                [<make_ $type_lower _stop>](vec![].into(), path.join("1")),
                            ],
                        ),
                    ];

                    for (directory_operation, cases) in test_cases {
                        match directory_operation {
                            DirectoryOperation::None => {}
                            DirectoryOperation::Create(path) => {
                                fs::create_dir(&path).expect(&path.to_string_lossy())
                            }
                            DirectoryOperation::Remove(path) => {
                                fs::remove_dir_all(&path).expect(&path.to_string_lossy())
                            }
                        }
                        for case in cases {
                            assert_eq!(case, await!(rx.next()).unwrap());
                        }
                    }
                }
                        }
        };
    }

    enum Never {}

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

        let thread_path = path.clone();
        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let collector = ExtraDataCollector::new();

                await!(collector.clone().watch(path)).unwrap();

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

    make_hub_directory_watcher_test!(
        component_instance_watcher,
        component_instance,
        ComponentInstance
    );
    make_hub_directory_watcher_test!(
        component_instance_directory_watcher,
        component_instance_directory,
        ComponentInstanceDirectory
    );
    make_hub_directory_watcher_test!(
        realm_instance_directory_watcher,
        realm_instance_directory,
        RealmInstanceDirectory
    );
    make_hub_directory_watcher_test!(realm_directory_watcher, realm_directory, RealmInstance);

    #[fasync::run_until_stalled(test)]
    async fn next_watchers_for_realm_instance() {
        let path = PathBuf::from("/hub");
        let context = HubContext::new();

        assert_eq!(
            vec![
                NextHubWatcherParams {
                    path: path.join("r"),
                    hub_context: context.clone(),
                    hub_event: HubEvent::RealmInstanceDirectory(None)
                },
                NextHubWatcherParams {
                    path: path.join("c"),
                    hub_context: context.clone(),
                    hub_event: HubEvent::ComponentInstanceDirectory(None)
                }
            ],
            next_watchers_for_event(&HubEvent::RealmInstance(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Existing(path.clone())
            })))
        );
        assert_eq!(
            vec![
                NextHubWatcherParams {
                    path: path.join("r"),
                    hub_context: context.not_existing(),
                    hub_event: HubEvent::RealmInstanceDirectory(None)
                },
                NextHubWatcherParams {
                    path: path.join("c"),
                    hub_context: context.not_existing(),
                    hub_event: HubEvent::ComponentInstanceDirectory(None)
                }
            ],
            next_watchers_for_event(&HubEvent::RealmInstance(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Added(path.clone())
            })))
        );
        assert_eq!(
            Vec::<NextHubWatcherParams>::new(),
            next_watchers_for_event(&HubEvent::RealmInstance(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Removed(path.clone())
            })))
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn next_watchers_for_realm_instance_directory() {
        let path = PathBuf::from("/hub/r/sys");
        let context = HubContext::new();

        assert_eq!(
            vec![NextHubWatcherParams {
                path: path.clone(),
                hub_context: context.join("sys"),
                hub_event: HubEvent::RealmInstance(None)
            },],
            next_watchers_for_event(&HubEvent::RealmInstanceDirectory(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Existing(path.clone())
            })))
        );
        assert_eq!(
            vec![NextHubWatcherParams {
                path: path.clone(),
                hub_context: context.join_created("sys"),
                hub_event: HubEvent::RealmInstance(None)
            },],
            next_watchers_for_event(&HubEvent::RealmInstanceDirectory(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Added(path.clone())
            })))
        );
        assert_eq!(
            Vec::<NextHubWatcherParams>::new(),
            next_watchers_for_event(&HubEvent::RealmInstanceDirectory(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Removed(path.clone())
            })))
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn next_watchers_for_component_instance_directory() {
        let path = PathBuf::from("/hub/c/test.cmx");
        let context = HubContext::new();

        assert_eq!(
            vec![NextHubWatcherParams {
                path: path.clone(),
                hub_context: context.clone(),
                hub_event: HubEvent::ComponentInstance(None)
            },],
            next_watchers_for_event(&HubEvent::ComponentInstanceDirectory(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Existing(path.clone())
            })))
        );
        assert_eq!(
            vec![NextHubWatcherParams {
                path: path.clone(),
                hub_context: context.not_existing(),
                hub_event: HubEvent::ComponentInstance(None)
            },],
            next_watchers_for_event(&HubEvent::ComponentInstanceDirectory(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Added(path.clone())
            })))
        );
        assert_eq!(
            Vec::<NextHubWatcherParams>::new(),
            next_watchers_for_event(&HubEvent::ComponentInstanceDirectory(Some(HubEventData {
                hub_context: context.clone(),
                path_event: PathEvent::Removed(path.clone())
            })))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn hub_collection() {
        let path = tempfile::tempdir().unwrap().into_path();

        let mut collector = HubCollector::new(path.clone()).unwrap();
        let mut component_events = collector.component_events().unwrap();
        fasync::spawn(
            collector
                .hub_events()
                .unwrap()
                .for_each(async move |x| eprintln!("HUB EVENT ---:\n{:?}\n----", x))
                .then(async move |_| ()),
        );

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
                Some(HashMap::new()),
            ),
            await!(component_events.next()).unwrap()
        );

        fs::remove_dir_all(path.join("c")).unwrap();
        assert_eq!(
            make_stop("my_component.cmx", "10", Some(HashMap::new()),),
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
            make_stop_with_realm(
                vec!["app".to_string()],
                "with_runner.cmx",
                "1",
                Some(HashMap::new())
            ),
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
}
