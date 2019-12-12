// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError, moniker::AbsoluteMoniker, realm::Realm, resolver::ResolverRegistry,
        runner::Runner,
    },
    std::sync::Arc,
};

/// Holds configuration options for the component manager.
#[derive(Clone)]
pub struct ComponentManagerConfig {
    /// How many children, maximum, are returned by a call to `ChildIterator.next()`.
    pub list_children_batch_size: usize,
}

impl ComponentManagerConfig {
    pub fn default() -> Self {
        ComponentManagerConfig { list_children_batch_size: 1000 }
    }
}

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    /// The URL of the root component.
    pub root_component_url: String,
    /// The component resolver registry used in the root realm.
    /// In particular, it will be used to resolve the root component itself.
    pub root_resolver_registry: ResolverRegistry,
    /// The built-in ELF runner, used for starting components with an ELF binary.
    pub elf_runner: Arc<dyn Runner + Send + Sync + 'static>,
}

/// The component model holds authoritative state about a tree of component instances, including
/// each instance's identity, lifecycle, capabilities, and topological relationships.  It also
/// provides operations for instantiating, destroying, querying, and controlling component
/// instances at runtime.
///
/// To facilitate unit testing, the component model does not directly perform IPC.  Instead, it
/// delegates external interfacing concerns to other objects that implement traits such as
/// `Runner` and `Resolver`.
#[derive(Clone)]
pub struct Model {
    pub root_realm: Arc<Realm>,

    /// The built-in ELF runner, used for starting components with an ELF binary.
    // TODO(fxb/4761): Remove. This should be a routed capability, and
    // not explicitly passed around in the model.
    pub elf_runner: Arc<dyn Runner + Send + Sync>,
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        Model {
            root_realm: Arc::new(Realm::new_root_realm(
                params.root_resolver_registry,
                params.root_component_url,
            )),
            elf_runner: params.elf_runner,
        }
    }

    /// Looks up a realm by absolute moniker. The component instance in the realm will be resolved
    /// if that has not already happened.
    pub async fn look_up_realm(
        &self,
        look_up_abs_moniker: &AbsoluteMoniker,
    ) -> Result<Arc<Realm>, ModelError> {
        let mut cur_realm = self.root_realm.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur_realm = {
                Realm::resolve_decl(&cur_realm).await?;
                let cur_state = cur_realm.lock_state().await;
                let cur_state = cur_state.as_ref().expect("look_up_realm: not resolved");
                if let Some(r) = cur_state.all_child_realms().get(moniker) {
                    r.clone()
                } else {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
            };
        }
        Realm::resolve_decl(&cur_realm).await?;
        Ok(cur_realm)
    }
}
