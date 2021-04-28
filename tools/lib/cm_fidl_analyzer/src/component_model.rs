// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_tree::{ComponentNode, ComponentTree, NodeEnvironment, NodePath},
    async_trait::async_trait,
    cm_rust::ComponentDecl,
    moniker::{AbsoluteMoniker, ChildMoniker, PartialMoniker},
    routing::{
        capability_source::NamespaceCapabilities,
        component_id_index::ComponentIdIndex,
        component_instance::{
            ComponentInstanceInterface, ExtendedInstanceInterface, TopInstanceInterface,
            WeakExtendedInstanceInterface,
        },
        config::RuntimeConfig,
        environment::{DebugRegistry, EnvironmentExtends, EnvironmentInterface, RunnerRegistry},
        error::ComponentInstanceError,
        policy::GlobalPolicyChecker,
    },
    std::{
        collections::HashMap,
        sync::{Arc, RwLock},
    },
};

/// Builds a `ComponentModelForAnalyzer` from a `ComponentTree` and a `RuntimeConfig`.
pub struct ModelBuilderForAnalyzer {}

impl ModelBuilderForAnalyzer {
    pub fn new() -> Self {
        Self {}
    }

    pub async fn build(
        self,
        tree: ComponentTree,
        runtime_config: Arc<RuntimeConfig>,
    ) -> anyhow::Result<Arc<ComponentModelForAnalyzer>> {
        let mut model = ComponentModelForAnalyzer {
            top_instance: TopInstanceForAnalyzer::new(
                runtime_config.namespace_capabilities.clone(),
            ),
            instances: HashMap::new(),
            policy_checker: GlobalPolicyChecker::new(Arc::clone(&runtime_config)),
            component_id_index: Arc::new(ComponentIdIndex::default()),
        };
        if let Some(ref index_path) = runtime_config.component_id_index_path {
            model.component_id_index = Arc::new(ComponentIdIndex::new(index_path).await?);
        }
        let root = tree.get_root_node()?;
        self.build_realm(root, &tree, &mut model)?;
        Ok(Arc::new(model))
    }

    fn build_realm(
        &self,
        node: &ComponentNode,
        tree: &ComponentTree,
        model: &mut ComponentModelForAnalyzer,
    ) -> anyhow::Result<Arc<ComponentInstanceForAnalyzer>> {
        let abs_moniker =
            AbsoluteMoniker::parse_string_without_instances(&node.node_path().to_string())
                .expect("failed to parse moniker from id");
        let parent = match node.parent() {
            Some(parent_id) => ExtendedInstanceInterface::Component(Arc::clone(
                model.instances.get(&parent_id).expect("parent instance not found"),
            )),
            None => ExtendedInstanceInterface::AboveRoot(Arc::clone(&model.top_instance)),
        };
        let environment = EnvironmentForAnalyzer::new(
            node.environment().clone(),
            WeakExtendedInstanceInterface::from(&parent),
        );
        let instance = Arc::new(ComponentInstanceForAnalyzer {
            abs_moniker,
            decl: node.decl.clone(),
            parent: WeakExtendedInstanceInterface::from(&parent),
            children: RwLock::new(HashMap::new()),
            environment,
            policy_checker: model.policy_checker.clone(),
            component_id_index: Arc::clone(&model.component_id_index),
        });
        model.instances.insert(node.node_path(), Arc::clone(&instance));
        self.build_children(node, tree, model)?;
        Ok(instance)
    }

    fn build_children(
        &self,
        node: &ComponentNode,
        tree: &ComponentTree,
        model: &mut ComponentModelForAnalyzer,
    ) -> anyhow::Result<()> {
        for child_id in node.children().iter() {
            let child_instance = self.build_realm(tree.get_node(child_id)?, tree, model)?;
            let partial_moniker = ChildMoniker::to_partial(
                child_instance
                    .abs_moniker()
                    .leaf()
                    .expect("expected child instance to have partial moniker"),
            );
            model
                .instances
                .get(&node.node_path())
                .expect("instance id not found")
                .children
                .write()
                .expect("failed to acquire write lock")
                .insert(partial_moniker, child_instance);
        }
        Ok(())
    }
}

/// `ComponentModelForAnalzyer` owns a representation of each v2 component instance and
/// supports lookup by `NodePath`.
pub struct ComponentModelForAnalyzer {
    top_instance: Arc<TopInstanceForAnalyzer>,
    instances: HashMap<NodePath, Arc<ComponentInstanceForAnalyzer>>,
    policy_checker: GlobalPolicyChecker,
    component_id_index: Arc<ComponentIdIndex>,
}

impl ComponentModelForAnalyzer {
    /// Returns the number of component instances in the model, not counting the top instance.
    pub fn len(&self) -> usize {
        self.instances.len()
    }

    /// Returns the component instance corresponding to `id` if it is present in the model, or an
    /// `InstanceNotFound` error if not.
    pub fn get_instance(
        self: &Arc<Self>,
        id: &NodePath,
    ) -> Result<Arc<ComponentInstanceForAnalyzer>, ComponentInstanceError> {
        let abs_moniker = AbsoluteMoniker::parse_string_without_instances(&id.to_string())
            .expect("failed to parse moniker from id");
        match self.instances.get(id) {
            Some(instance) => Ok(Arc::clone(instance)),
            None => Err(ComponentInstanceError::instance_not_found(abs_moniker)),
        }
    }
}

/// A representation of a v2 component instance.
pub struct ComponentInstanceForAnalyzer {
    abs_moniker: AbsoluteMoniker,
    decl: ComponentDecl,
    parent: WeakExtendedInstanceInterface<ComponentInstanceForAnalyzer>,
    children: RwLock<HashMap<PartialMoniker, Arc<ComponentInstanceForAnalyzer>>>,
    environment: Arc<EnvironmentForAnalyzer>,
    policy_checker: GlobalPolicyChecker,
    component_id_index: Arc<ComponentIdIndex>,
}

/// A representation of `ComponentManager`'s instance, providing a set of capabilities to
/// the root component instance.
#[derive(Debug)]
pub struct TopInstanceForAnalyzer {
    namespace_capabilities: NamespaceCapabilities,
}

#[async_trait]
impl ComponentInstanceInterface for ComponentInstanceForAnalyzer {
    type TopInstance = TopInstanceForAnalyzer;

    fn abs_moniker(&self) -> &AbsoluteMoniker {
        &self.abs_moniker
    }

    fn environment(&self) -> &dyn EnvironmentInterface<Self> {
        self.environment.as_ref()
    }

    fn try_get_parent(&self) -> Result<ExtendedInstanceInterface<Self>, ComponentInstanceError> {
        Ok(self.parent.upgrade()?)
    }

    fn try_get_policy_checker(&self) -> Result<GlobalPolicyChecker, ComponentInstanceError> {
        Ok(self.policy_checker.clone())
    }

    fn try_get_component_id_index(&self) -> Result<Arc<ComponentIdIndex>, ComponentInstanceError> {
        Ok(Arc::clone(&self.component_id_index))
    }

    async fn decl<'a>(self: &'a Arc<Self>) -> Result<ComponentDecl, ComponentInstanceError> {
        Ok(self.decl.clone())
    }

    async fn get_live_child<'a>(
        self: &'a Arc<Self>,
        moniker: &PartialMoniker,
    ) -> Result<Option<Arc<Self>>, ComponentInstanceError> {
        match self.children.read().expect("failed to acquire read lock").get(moniker) {
            Some(child) => Ok(Some(Arc::clone(child))),
            None => Ok(None),
        }
    }

    // This is a static model with no notion of a collection.
    async fn live_children_in_collection<'a>(
        self: &'a Arc<Self>,
        _collection: &'a str,
    ) -> Result<Vec<(PartialMoniker, Arc<Self>)>, ComponentInstanceError> {
        Ok(vec![])
    }
}

impl TopInstanceForAnalyzer {
    fn new(namespace_capabilities: NamespaceCapabilities) -> Arc<Self> {
        Arc::new(Self { namespace_capabilities })
    }
}

impl TopInstanceInterface for TopInstanceForAnalyzer {
    fn namespace_capabilities(&self) -> &NamespaceCapabilities {
        &self.namespace_capabilities
    }
}

/// A representation of a v2 component instance's environment and its relationship to the
/// parent realm's environment.
pub struct EnvironmentForAnalyzer {
    environment: NodeEnvironment,
    parent: WeakExtendedInstanceInterface<ComponentInstanceForAnalyzer>,
}

impl EnvironmentForAnalyzer {
    fn new(
        environment: NodeEnvironment,
        parent: WeakExtendedInstanceInterface<ComponentInstanceForAnalyzer>,
    ) -> Arc<Self> {
        Arc::new(Self { environment, parent })
    }
}

impl EnvironmentInterface<ComponentInstanceForAnalyzer> for EnvironmentForAnalyzer {
    fn name(&self) -> Option<&str> {
        self.environment.name()
    }

    fn parent(&self) -> &WeakExtendedInstanceInterface<ComponentInstanceForAnalyzer> {
        &self.parent
    }

    fn extends(&self) -> &EnvironmentExtends {
        self.environment.extends()
    }

    fn runner_registry(&self) -> &RunnerRegistry {
        self.environment.runner_registry()
    }

    fn debug_registry(&self) -> &DebugRegistry {
        self.environment.debug_registry()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::capability_routing::testing::build_two_node_tree, anyhow::Result,
        futures::executor::block_on,
    };

    // Builds a model from a 2-node `ComponentTree` with structure `root -- child`, retrieves
    // each of the 2 resulting component instances, and tests their public methods.
    #[test]
    fn build_model() -> Result<()> {
        let tree =
            build_two_node_tree(vec![], vec![], vec![], vec![], vec![], vec![]).tree.unwrap();
        let config = Arc::new(RuntimeConfig::default());
        let model = block_on(async { ModelBuilderForAnalyzer::new().build(tree, config).await })?;
        assert_eq!(model.len(), 2);

        let child_moniker = PartialMoniker::new("child".to_string(), None);
        let root_id = NodePath::new(vec![]);
        let child_id = root_id.extended(child_moniker.clone());
        let other_id = root_id.extended(PartialMoniker::new("other".to_string(), None));

        let root_instance = model.get_instance(&root_id)?;
        let child_instance = model.get_instance(&child_id)?;

        let get_other_result = model.get_instance(&other_id);
        assert_eq!(
            get_other_result.err().unwrap().to_string(),
            ComponentInstanceError::instance_not_found(
                AbsoluteMoniker::parse_string_without_instances(&other_id.to_string())
                    .expect("failed to parse moniker from id")
            )
            .to_string()
        );

        assert_eq!(root_instance.abs_moniker(), &AbsoluteMoniker::root());
        assert_eq!(
            child_instance.abs_moniker(),
            &AbsoluteMoniker::parse_string_without_instances("/child")
                .expect("failed to parse moniker from id")
        );

        match root_instance.try_get_parent()? {
            ExtendedInstanceInterface::AboveRoot(_) => {}
            _ => {
                panic!("root instance's parent should be `AboveRoot`")
            }
        }
        match child_instance.try_get_parent()? {
            ExtendedInstanceInterface::Component(component) => {
                assert_eq!(component.abs_moniker(), root_instance.abs_moniker())
            }
            _ => panic!("child instance's parent should be root component"),
        }

        let get_child = block_on(async { root_instance.get_live_child(&child_moniker).await })?;
        assert!(get_child.is_some());
        assert_eq!(get_child.unwrap().abs_moniker(), child_instance.abs_moniker());

        let root_environment = root_instance.environment();
        let child_environment = child_instance.environment();

        assert_eq!(root_environment.name(), None);
        match root_environment.parent() {
            WeakExtendedInstanceInterface::AboveRoot(_) => {}
            _ => {
                panic!("root environment's parent should be `AboveRoot`")
            }
        }

        assert_eq!(child_environment.name(), None);
        match child_environment.parent() {
            WeakExtendedInstanceInterface::Component(component) => {
                assert_eq!(component.upgrade()?.abs_moniker(), root_instance.abs_moniker())
            }
            _ => panic!("child environment's parent should be root component"),
        }

        root_instance.try_get_policy_checker()?;
        root_instance.try_get_component_id_index()?;

        child_instance.try_get_policy_checker()?;
        child_instance.try_get_component_id_index()?;

        block_on(async {
            assert!(root_instance.decl().await.is_ok());
            assert!(child_instance.decl().await.is_ok());
        });

        Ok(())
    }
}
