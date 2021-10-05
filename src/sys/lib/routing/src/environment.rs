// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        component_instance::{
            ComponentInstanceInterface, ExtendedInstanceInterface, WeakExtendedInstanceInterface,
        },
        error::ComponentInstanceError,
    },
    cm_rust::{
        CapabilityName, RegistrationDeclCommon, RegistrationSource, RunnerRegistration, SourceName,
    },
    fidl_fuchsia_sys2 as fsys,
    moniker::AbsoluteMonikerBase,
    std::{collections::HashMap, sync::Arc},
    url::Url,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// A trait providing data from a component instance's environment.
pub trait EnvironmentInterface<C>: Send + Sync
where
    C: ComponentInstanceInterface,
{
    /// Returns the runner registered to `name` and the component that created the environment the
    /// runner was registered to. Returns `None` if there was no match.
    fn get_registered_runner(
        &self,
        name: &CapabilityName,
    ) -> Result<Option<(ExtendedInstanceInterface<C>, RunnerRegistration)>, ComponentInstanceError>
    {
        let parent = self.parent().upgrade()?;
        match self.runner_registry().get_runner(name) {
            Some(reg) => Ok(Some((parent, reg.clone()))),
            None => match self.extends() {
                EnvironmentExtends::Realm => match parent {
                    ExtendedInstanceInterface::<C>::Component(parent) => {
                        parent.environment().get_registered_runner(name)
                    }
                    ExtendedInstanceInterface::<C>::AboveRoot(_) => {
                        unreachable!("root env can't extend")
                    }
                },
                EnvironmentExtends::None => {
                    return Ok(None);
                }
            },
        }
    }

    /// Returns the debug capability registered to `name`, the realm that created the environment
    /// and the capability was registered to (`None` for component manager's realm) and name of the
    /// environment that registered the capability. Returns `None` if there was no match.
    fn get_debug_capability(
        &self,
        name: &CapabilityName,
    ) -> Result<
        Option<(ExtendedInstanceInterface<C>, Option<String>, DebugRegistration)>,
        ComponentInstanceError,
    > {
        let parent = self.parent().upgrade()?;
        match self.debug_registry().get_capability(name) {
            Some(reg) => Ok(Some((parent, self.name().map(String::from), reg.clone()))),
            None => match self.extends() {
                EnvironmentExtends::Realm => match parent {
                    ExtendedInstanceInterface::<C>::Component(parent) => {
                        parent.environment().get_debug_capability(name)
                    }
                    ExtendedInstanceInterface::<C>::AboveRoot(_) => {
                        unreachable!("root env can't extend")
                    }
                },
                EnvironmentExtends::None => {
                    return Ok(None);
                }
            },
        }
    }

    /// The name of this environment as defined by its creator.
    /// Should be `None` for the root environment.
    fn name(&self) -> Option<&str>;

    /// The parent component instance or top instance that created or inherited the environment.
    fn parent(&self) -> &WeakExtendedInstanceInterface<C>;

    /// The relationship of this environment to that of the component instance's parent.
    fn extends(&self) -> &EnvironmentExtends;

    /// The runners available in this environment.
    fn runner_registry(&self) -> &RunnerRegistry;

    /// Protocols avaliable in this environment as debug capabilities.
    fn debug_registry(&self) -> &DebugRegistry;
}

/// How this environment extends its parent's.
#[derive(Debug, Clone, PartialEq)]
pub enum EnvironmentExtends {
    /// This environment extends the environment of its parent's.
    Realm,
    /// This environment was created from scratch.
    None,
}

impl From<fsys::EnvironmentExtends> for EnvironmentExtends {
    fn from(e: fsys::EnvironmentExtends) -> Self {
        match e {
            fsys::EnvironmentExtends::Realm => Self::Realm,
            fsys::EnvironmentExtends::None => Self::None,
        }
    }
}

/// The set of runners available in a realm's environment.
///
/// [`RunnerRegistration`]: fidl_fuchsia_sys2::RunnerRegistration
#[derive(Clone, Debug)]
pub struct RunnerRegistry {
    runners: HashMap<CapabilityName, RunnerRegistration>,
}

impl RunnerRegistry {
    pub fn default() -> Self {
        Self { runners: HashMap::new() }
    }

    pub fn new(runners: HashMap<CapabilityName, RunnerRegistration>) -> Self {
        Self { runners }
    }

    pub fn from_decl(regs: &Vec<RunnerRegistration>) -> Self {
        let mut runners = HashMap::new();
        for reg in regs {
            runners.insert(reg.target_name.clone(), reg.clone());
        }
        Self { runners }
    }
    pub fn get_runner(&self, name: &CapabilityName) -> Option<&RunnerRegistration> {
        self.runners.get(name)
    }
}

/// The set of debug capabilities available in this environment.
#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub struct DebugRegistry {
    pub debug_capabilities: HashMap<CapabilityName, DebugRegistration>,
}

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DebugRegistration {
    pub source: RegistrationSource,
    pub source_name: CapabilityName,
}

impl SourceName for DebugRegistration {
    fn source_name(&self) -> &CapabilityName {
        &self.source_name
    }
}

impl RegistrationDeclCommon for DebugRegistration {
    const TYPE: &'static str = "protocol";

    fn source(&self) -> &RegistrationSource {
        &self.source
    }
}

impl From<Vec<cm_rust::DebugRegistration>> for DebugRegistry {
    fn from(regs: Vec<cm_rust::DebugRegistration>) -> Self {
        let mut debug_capabilities = HashMap::new();
        for reg in regs {
            match reg {
                cm_rust::DebugRegistration::Protocol(r) => {
                    debug_capabilities.insert(
                        r.target_name,
                        DebugRegistration { source_name: r.source_name, source: r.source },
                    );
                }
            };
        }
        Self { debug_capabilities }
    }
}

impl DebugRegistry {
    pub fn get_capability(&self, name: &CapabilityName) -> Option<&DebugRegistration> {
        self.debug_capabilities.get(name)
    }
}

pub fn component_has_relative_url<C: ComponentInstanceInterface>(component: &Arc<C>) -> bool {
    Url::parse(component.url()) == Err(url::ParseError::RelativeUrlWithoutBase)
}

pub fn find_first_absolute_ancestor_url<C: ComponentInstanceInterface>(
    component: &Arc<C>,
) -> Result<Url, ComponentInstanceError> {
    let mut parent = component.try_get_parent()?;
    loop {
        match parent {
            ExtendedInstanceInterface::Component(parent_component) => {
                if !component_has_relative_url(&parent_component) {
                    let parent_url = Url::parse(parent_component.url()).map_err(|_| {
                        ComponentInstanceError::MalformedUrl {
                            url: parent_component.url().to_string(),
                            moniker: parent_component.abs_moniker().to_partial(),
                        }
                    })?;
                    return Ok(parent_url);
                }
                parent = parent_component.try_get_parent()?;
            }
            ExtendedInstanceInterface::AboveRoot(_) => {
                return Err(ComponentInstanceError::NoAbsoluteUrl {
                    url: component.url().to_string(),
                    moniker: component.abs_moniker().to_partial(),
                });
            }
        }
    }
}
