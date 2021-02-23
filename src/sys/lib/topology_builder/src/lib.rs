// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::*,
    cm_rust::{self, NativeIntoFidl},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fidl_fuchsia_topology_builder as ftopologybuilder,
    fuchsia_component::client as fclient,
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, FutureExt},
    std::{
        collections::HashMap,
        convert::TryInto,
        fmt::{self, Display},
    },
};

const DEFAULT_COLLECTION_NAME: &'static str = "topology_builder_collection";
const FRAMEWORK_INTERMEDIARY_CHILD_NAME: &'static str = "topology_builder_framework_intermediary";

pub mod builder;
pub mod error;
pub mod mock;

#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Moniker {
    path: Vec<String>,
}

impl From<&str> for Moniker {
    fn from(s: &str) -> Self {
        Moniker {
            path: match s {
                "" => vec![],
                _ => s.split('/').map(|s| s.to_string()).collect(),
            },
        }
    }
}

impl From<String> for Moniker {
    fn from(s: String) -> Self {
        s.as_str().into()
    }
}

impl From<Vec<String>> for Moniker {
    fn from(path: Vec<String>) -> Self {
        Moniker { path }
    }
}

impl Display for Moniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.is_root() {
            write!(f, "<root of topology>")
        } else {
            write!(f, "{}", self.path.join("/"))
        }
    }
}

impl Moniker {
    fn root() -> Self {
        Moniker { path: vec![] }
    }
    fn is_root(&self) -> bool {
        return self.path.is_empty();
    }

    fn child_name(&self) -> Option<&String> {
        self.path.last()
    }

    fn path(&self) -> &Vec<String> {
        &self.path
    }

    fn child(&self, child_name: String) -> Self {
        let mut path = self.path.clone();
        path.push(child_name);
        Moniker { path }
    }

    /// Returns the list of components comprised of this component's parent, then that component's
    /// parent, and so on. This list does not include the root component.
    ///
    /// For example, `"a/b/c/d".into().ancestry()` would return `vec!["a/b/c".into(), "a/b".into(),
    /// "a".into()]`
    fn ancestry(&self) -> Vec<Moniker> {
        let mut current_moniker = Moniker { path: vec![] };
        let mut res = vec![];
        let mut parent_path = self.path.clone();
        parent_path.pop();
        for part in parent_path {
            current_moniker.path.push(part.clone());
            res.push(current_moniker.clone());
        }
        res
    }

    fn parent(&self) -> Option<Self> {
        let mut path = self.path.clone();
        path.pop()?;
        Some(Moniker { path })
    }

    fn is_ancestor_of(&self, other_moniker: &Moniker) -> bool {
        if self.path.len() >= other_moniker.path.len() {
            return false;
        }
        for (element_from_us, element_from_them) in self.path.iter().zip(other_moniker.path.iter())
        {
            if element_from_us != element_from_them {
                return false;
            }
        }
        return true;
    }
}

#[derive(Debug, Clone)]
struct TopologyNode {
    decl: cm_rust::ComponentDecl,
    eager: bool,
    children: HashMap<String, TopologyNode>,
}

impl TopologyNode {
    fn new(decl: cm_rust::ComponentDecl) -> Self {
        Self { decl, eager: false, children: HashMap::new() }
    }
}

/// A running instance of a created [`Topology`]. When this struct is dropped the child components
/// are destroyed.
pub struct TopologyInstance {
    /// The root component of this topology instance, which can be used to access exposed
    /// capabilities from the topology.
    pub root: fclient::ScopedInstance,
    // We want to ensure that the mocks runner remains alive for as long as the topology exists, so
    // the ScopedInstance is bundled up into a struct along with the mocks runner.
    _mocks_runner: mock::MocksRunner,
}

/// A custom built topology, which can be created at runtime in a component collection
pub struct Topology {
    decl_tree: Option<TopologyNode>,
    framework_intermediary_proxy: ftopologybuilder::FrameworkIntermediaryProxy,
    mocks_runner: mock::MocksRunner,
    collection_name: String,
}

impl Topology {
    pub async fn new() -> Result<Self, Error> {
        let realm_proxy = fclient::connect_to_service::<fsys::RealmMarker>()
            .map_err(TopologyError::ConnectToRealmService)?;
        let (exposed_dir_proxy, exposed_dir_server_end) =
            create_proxy::<fio::DirectoryMarker>().map_err(TopologyError::CreateProxy)?;
        realm_proxy
            .bind_child(
                &mut fsys::ChildRef {
                    name: FRAMEWORK_INTERMEDIARY_CHILD_NAME.to_string(),
                    collection: None,
                },
                exposed_dir_server_end,
            )
            .await
            .map_err(TopologyError::FailedToUseRealm)?
            .map_err(TopologyError::FailedBindToFrameworkIntermediary)?;
        let framework_intermediary_proxy = fclient::connect_to_protocol_at_dir_root::<
            ftopologybuilder::FrameworkIntermediaryMarker,
        >(&exposed_dir_proxy)
        .map_err(TopologyError::ConnectToFrameworkIntermediaryService)?;

        Topology::new_with_framework_intermediary_proxy(framework_intermediary_proxy)
    }

    fn new_with_framework_intermediary_proxy(
        framework_intermediary_proxy: ftopologybuilder::FrameworkIntermediaryProxy,
    ) -> Result<Self, Error> {
        let mocks_runner = mock::MocksRunner::new(framework_intermediary_proxy.take_event_stream());
        Ok(Self {
            decl_tree: None,
            framework_intermediary_proxy,
            mocks_runner,
            collection_name: DEFAULT_COLLECTION_NAME.to_string(),
        })
    }

    /// Adds a new mocked component to the topology. When the component is supposed to run the
    /// provided [`Mock`] is called with the component's handles.
    pub async fn add_mocked_component(
        &mut self,
        moniker: Moniker,
        mock: mock::Mock,
    ) -> Result<(), Error> {
        let mock_id = self
            .framework_intermediary_proxy
            .register_mock()
            .await
            .map_err(TopologyError::FailedToUseFrameworkIntermediary)?;
        self.mocks_runner.register_mock(mock_id.clone(), mock).await;
        let decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: mock::MOCK_ID_KEY.to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(mock_id))),
                    }]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            ..cm_rust::ComponentDecl::default()
        };
        self.add_component(moniker, decl)
    }

    /// Adds a new component to the topology. Note that the provided `ComponentDecl` should not
    /// have child declarations for other components described in this `Topology`, as those will be
    /// filled in when [`Topology::create`] is called.
    pub fn add_component(
        &mut self,
        moniker: Moniker,
        decl: cm_rust::ComponentDecl,
    ) -> Result<(), Error> {
        if moniker.is_root() {
            if self.decl_tree.is_some() {
                return Err(TopologyError::RootComponentAlreadyExists.into());
            }
            // Validation is performed later, as we might end up adding ChildDecls to this
            self.decl_tree = Some(TopologyNode::new(decl));
            return Ok(());
        }
        if self.decl_tree.is_none() {
            return Err(TopologyError::RootComponentNotSetYet.into());
        }
        let parent_node = self.get_node_mut(&moniker.parent().unwrap())?;

        if parent_node.children.contains_key(moniker.child_name().unwrap())
            || parent_node.decl.children.iter().any(|c| &c.name == moniker.child_name().unwrap())
        {
            return Err(TopologyError::ComponentAlreadyExists(moniker).into());
        }

        // Validation is performed later, as we might end up adding ChildDecls to this
        parent_node.children.insert(moniker.child_name().unwrap().clone(), TopologyNode::new(decl));
        Ok(())
    }

    fn get_node_mut<'a>(&'a mut self, moniker: &Moniker) -> Result<&'a mut TopologyNode, Error> {
        if self.decl_tree.is_none() {
            return Err(TopologyError::RootComponentNotSetYet.into());
        }
        let mut current_node = self.decl_tree.as_mut().unwrap();
        let mut current_moniker = Moniker::root();

        for part in moniker.path() {
            current_moniker = current_moniker.child(part.clone());
            current_node = match current_node.children.get_mut(part) {
                Some(n) => n,
                None => {
                    if current_node.decl.children.iter().any(|c| &c.name == part) {
                        return Err(TopologyError::ComponentNotModifiable(current_moniker).into());
                    }
                    return Err(TopologyError::ComponentDoesntExist(current_moniker).into());
                }
            }
        }
        Ok(current_node)
    }

    /// Returns whether or not the given component exists in this topology. This will return true
    /// if the component exists in the topology tree itself, or if the parent contains a child
    /// declaration for the moniker.
    pub fn contains(&mut self, moniker: &Moniker) -> bool {
        if let Ok(_) = self.get_node_mut(moniker) {
            return true;
        }
        if let Some(parent) = moniker.parent() {
            if let Ok(decl) = self.get_decl_mut(&parent) {
                return decl.children.iter().any(|c| Some(&c.name) == moniker.child_name());
            }
        }
        return false;
    }

    /// Returns a mutable reference to a component decl in the topology.
    pub fn get_decl_mut<'a>(
        &'a mut self,
        moniker: &Moniker,
    ) -> Result<&'a mut cm_rust::ComponentDecl, Error> {
        Ok(&mut self.get_node_mut(moniker)?.decl)
    }

    /// Marks the target component as eager.
    ///
    /// If the target component is a component that was added to this topology with
    /// [`Topology::add_component`], then the component is marked as eager in the Topology's
    /// internal structure. If the target component is a component referenced in an added
    /// component's [`cm_rust::ComponentDecl`], then the `ChildDecl` for the component is modified.
    pub fn mark_as_eager(&mut self, moniker: &Moniker) -> Result<(), Error> {
        if moniker.is_root() {
            return Err(TopologyError::CantMarkRootAsEager.into());
        }
        // The referenced moniker might be a node in our local tree, or it might be a component
        // with a source outside of our tree (like fuchsia-pkg://). Attempt to look up the node in
        // our tree, and if it doesn't exist then look for a ChildDecl on the parent node
        match self.get_node_mut(moniker) {
            Ok(node) => node.eager = true,
            Err(e) => {
                let child_name = moniker.child_name().unwrap();
                let parent_node = self.get_node_mut(&moniker.parent().unwrap()).map_err(|_| e)?;
                let child_decl = parent_node
                    .decl
                    .children
                    .iter_mut()
                    .find(|c| &c.name == child_name)
                    .ok_or(TopologyError::MissingChild(moniker.clone()))?;
                child_decl.startup = fsys::StartupMode::Eager;
            }
        }
        Ok(())
    }

    /// Sets the name of the collection that this topology will be created in
    pub fn set_collection_name(&mut self, collection_name: impl Into<String>) {
        self.collection_name = collection_name.into();
    }

    /// Initializes the topology, but doesn't create it. Returns the root URL, the collection name,
    /// and the mocks runner. The caller should pass the URL and collection name into
    /// `fuchsial.sys2.Realm#CreateChild`, and keep the mocks runner alive until after
    /// `fuchsia.sys2.Realm#DestroyChild` has been called.
    pub async fn initialize(self) -> Result<(String, String, mock::MocksRunner), Error> {
        if self.decl_tree.is_none() {
            return Err(TopologyError::RootComponentNotSetYet.into());
        }
        let decl_tree = self.decl_tree.as_ref().unwrap().clone();
        let root_url =
            Self::registration_walker(self.framework_intermediary_proxy.clone(), decl_tree, vec![])
                .await?;
        Ok((root_url, self.collection_name, self.mocks_runner))
    }

    /// Creates this topology in a child component collection. By default this happens in the
    /// [`DEFAULT_COLLECTION_NAME`] collection.
    pub async fn create(self) -> Result<TopologyInstance, Error> {
        let (root_url, collection_name, mocks_runner) = self.initialize().await?;
        let root = fclient::ScopedInstance::new(collection_name, root_url)
            .await
            .map_err(TopologyError::FailedToCreateChild)?;
        Ok(TopologyInstance { root, _mocks_runner: mocks_runner })
    }

    /// Walks a topology, registering each node with the component resolver.
    fn registration_walker(
        framework_intermediary_proxy: ftopologybuilder::FrameworkIntermediaryProxy,
        mut current_node: TopologyNode,
        walked_path: Vec<String>,
    ) -> BoxFuture<'static, Result<String, Error>> {
        // This function is much cleaner written recursively, but we can't construct recursive
        // futures as the size isn't knowable to rustc at compile time. Put the recursive call
        // into a boxed future, as the redirection makes this possible
        async move {
            let mut children = current_node.children.into_iter().collect::<Vec<_>>();
            children.sort_unstable_by_key(|t| t.0.clone());
            for (name, node) in children {
                let mut new_path = walked_path.clone();
                new_path.push(name.clone());

                let startup =
                    if node.eager { fsys::StartupMode::Eager } else { fsys::StartupMode::Lazy };
                let url =
                    Self::registration_walker(framework_intermediary_proxy.clone(), node, new_path)
                        .await?;
                current_node.decl.children.push(cm_rust::ChildDecl {
                    name,
                    url,
                    startup,
                    environment: None,
                });
            }

            let fidl_decl = current_node.decl.native_into_fidl();
            cm_fidl_validator::validate(&fidl_decl).map_err(|e| {
                TopologyError::InvalidDecl(walked_path.into(), e, fidl_decl.clone())
            })?;

            framework_intermediary_proxy
                .register_decl(fidl_decl)
                .await
                .map_err(TopologyError::FailedToUseFrameworkIntermediary)?
                .map_err(|s| TopologyError::DeclRejectedByRegistry(zx::Status::from_raw(s)).into())
        }
        .boxed()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_rust::*,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_data as fdata, fidl_fuchsia_topology_builder as ftopologybuilder,
        fuchsia_async as fasync,
        futures::{lock::Mutex, TryStreamExt},
        std::{collections::HashSet, sync::Arc},
    };

    struct FrameworkIntermediaryMock {
        _stream_handling_task: fasync::Task<()>,

        _decls: Arc<Mutex<HashMap<String, ComponentDecl>>>,
        mocks: Arc<Mutex<HashSet<String>>>,
    }

    fn topology_with_mock_framework_intermediary() -> (FrameworkIntermediaryMock, Topology) {
        let (framework_intermediary_proxy, framework_intermediary_server_end) =
            create_proxy::<ftopologybuilder::FrameworkIntermediaryMarker>().unwrap();

        let decls = Arc::new(Mutex::new(HashMap::new()));
        let mocks = Arc::new(Mutex::new(HashSet::new()));

        let decls_clone = decls.clone();
        let mocks_clone = mocks.clone();
        let task = fasync::Task::local(async move {
            let mut framework_intermediary_stream =
                framework_intermediary_server_end.into_stream().unwrap();
            let mut mock_counter: u64 = 0;
            let mut next_unique_component_id: u64 = 0;
            while let Some(request) = framework_intermediary_stream.try_next().await.unwrap() {
                match request {
                    ftopologybuilder::FrameworkIntermediaryRequest::RegisterDecl {
                        responder,
                        decl,
                    } => {
                        let native_decl = decl.fidl_into_native();
                        let url = format!("{}://{}", mock::RUNNER_NAME, next_unique_component_id);
                        decls_clone.lock().await.insert(url.clone(), native_decl);
                        next_unique_component_id += 1;
                        responder.send(&mut Ok(url)).unwrap()
                    }
                    ftopologybuilder::FrameworkIntermediaryRequest::RegisterMock { responder } => {
                        let mock_id = format!("{}", mock_counter);
                        mocks_clone.lock().await.insert(mock_id.clone());
                        mock_counter += 1;
                        responder.send(&mock_id).unwrap();
                    }
                }
            }
        });
        let topology_with_mock_framework_intermediary =
            Topology::new_with_framework_intermediary_proxy(framework_intermediary_proxy).unwrap();

        (
            FrameworkIntermediaryMock { _stream_handling_task: task, _decls: decls, mocks },
            topology_with_mock_framework_intermediary,
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn set_root_decl() {
        let (_fi_mock, mut topology) = topology_with_mock_framework_intermediary();

        topology.add_component(Moniker::root(), ComponentDecl::default()).unwrap();
        assert_eq!(ComponentDecl::default(), *topology.get_decl_mut(&Moniker::root()).unwrap());
    }

    #[fasync::run_until_stalled(test)]
    async fn add_component() {
        let (fi_mock, mut topology) = topology_with_mock_framework_intermediary();

        let root_decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: OfferTarget::Child("a".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: DependencyType::Strong,
            })],
            ..ComponentDecl::default()
        };
        let a_decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: OfferTarget::Child("b".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: DependencyType::Strong,
            })],
            children: vec![ChildDecl {
                name: "b".to_string(),
                url: "fuchsia-pkg://b".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
            }],
            ..ComponentDecl::default()
        };

        topology.add_component(Moniker::root(), root_decl.clone()).unwrap();
        topology.add_component("a".into(), a_decl.clone()).unwrap();

        assert_eq!(fi_mock.mocks.lock().await.len(), 0);

        assert_eq!(*topology.get_decl_mut(&Moniker::root()).unwrap(), root_decl);
        assert_eq!(*topology.get_decl_mut(&"a".into()).unwrap(), a_decl);
    }

    #[fasync::run_until_stalled(test)]
    async fn add_mocked_component() {
        let (fi_mock, mut topology) = topology_with_mock_framework_intermediary();

        let root_decl = ComponentDecl {
            offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: OfferTarget::Child("a".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: DependencyType::Strong,
            })],
            ..ComponentDecl::default()
        };
        let use_echo_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
        });

        topology.add_component(Moniker::root(), root_decl.clone()).unwrap();
        topology
            .add_mocked_component(
                "a".into(),
                mock::Mock::new(|_: mock::MockHandles| Box::pin(async move { Ok(()) })),
            )
            .await
            .unwrap();

        topology.get_decl_mut(&"a".into()).unwrap().uses.push(use_echo_decl.clone());

        assert_eq!(fi_mock.mocks.lock().await.len(), 1);
        assert!(fi_mock.mocks.lock().await.contains(&"0".to_string()));

        assert_eq!(*topology.get_decl_mut(&Moniker::root()).unwrap(), root_decl);
        assert_eq!(
            *topology.get_decl_mut(&"a".into()).unwrap(),
            ComponentDecl {
                program: Some(cm_rust::ProgramDecl {
                    runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                    info: fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: mock::MOCK_ID_KEY.to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("0".to_string()))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    },
                }),
                uses: vec![use_echo_decl],
                ..ComponentDecl::default()
            }
        );
    }
}
