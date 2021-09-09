// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_rust::{ChildRef, ExposeSource, OfferSource, UseSource},
    std::{fmt, fmt::Display},
};

/// A type consolidating all options for the source of a capability route segment.
#[derive(Clone, Debug, PartialEq)]
pub enum CapabilitySourceType {
    Parent,
    Self_,
    Framework,
    RootParent,
    Decl,
    Child(String),
    Capability(String),
    Debug,
}

impl From<&UseSource> for CapabilitySourceType {
    fn from(source: &UseSource) -> Self {
        match source {
            UseSource::Parent => Self::Parent,
            UseSource::Framework => Self::Framework,
            UseSource::Debug => Self::Debug,
            UseSource::Capability(name) => Self::Capability(name.to_string()),
            UseSource::Child(name) => Self::Child(name.to_string()),
            UseSource::Self_ => Self::Self_,
        }
    }
}

impl From<&ExposeSource> for CapabilitySourceType {
    fn from(source: &ExposeSource) -> Self {
        match source {
            ExposeSource::Self_ => Self::Self_,
            ExposeSource::Child(name) => Self::Child(name.to_string()),
            ExposeSource::Framework => Self::Framework,
            ExposeSource::Capability(name) => Self::Capability(name.to_string()),
            ExposeSource::Collection(_) => panic!("no"),
        }
    }
}

impl From<&OfferSource> for CapabilitySourceType {
    fn from(source: &OfferSource) -> Self {
        match source {
            OfferSource::Framework => Self::Framework,
            OfferSource::Parent => Self::Parent,
            OfferSource::Child(ChildRef { name, collection }) => {
                // TODO(fxbug.dev/81207): This doesn't properly handle dynamic children.
                assert_eq!(collection, &None);
                Self::Child(name.to_string())
            }
            OfferSource::Self_ => Self::Self_,
            OfferSource::Capability(name) => Self::Capability(name.to_string()),
            OfferSource::Collection(_) => panic!("no"),
        }
    }
}

impl Display for CapabilitySourceType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &*self {
            CapabilitySourceType::Parent => {
                write!(f, "parent")
            }
            CapabilitySourceType::Self_ => {
                write!(f, "self")
            }
            CapabilitySourceType::Framework => {
                write!(f, "framework")
            }
            CapabilitySourceType::RootParent => {
                write!(f, "root parent")
            }
            CapabilitySourceType::Decl => {
                write!(f, "declaration")
            }
            CapabilitySourceType::Child(child) => {
                write!(f, "child `{}`", child)
            }
            CapabilitySourceType::Capability(capability) => {
                write!(f, "capability `{}`", capability)
            }
            CapabilitySourceType::Debug => {
                write!(f, "debug")
            }
        }
    }
}
