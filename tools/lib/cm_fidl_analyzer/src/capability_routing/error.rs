// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_tree::ComponentTreeError,
    serde::{Deserialize, Serialize},
    thiserror::Error,
};

#[derive(Clone, Debug, Deserialize, Error, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CapabilityRouteError {
    #[error("failed to find component: `{0}`")]
    ComponentNotFound(ComponentTreeError),
    #[error("no offer declaration for `{0}` with name `{1}`")]
    OfferDeclNotFound(String, String),
    #[error("multiple offer declarations found for `{0}` with name `{1}`")]
    DuplicateOfferDecl(String, String),
    #[error("no expose declaration for `{0}` with name `{1}`")]
    ExposeDeclNotFound(String, String),
    #[error("multiple expose declarations found for `{0}` with name `{1}`")]
    DuplicateExposeDecl(String, String),
    #[error("no capability declaration for `{0}` with name `{1}`")]
    CapabilityDeclNotFound(String, String),
    #[error("multiple capability declarations found for `{0}` with name `{1}`")]
    DuplicateCapabilityDecl(String, String),
    #[error("directory rights provided by `{0}` are not sufficient")]
    InvalidDirectoryRights(String),
    #[error("invalid source type: `{0}`")]
    InvalidSourceType(String),
    #[error("validation is not implemented for case: {0}")]
    ValidationNotImplemented(String),
    #[error("unexpected verifier state: {0}")]
    Internal(String),
}

impl From<ComponentTreeError> for CapabilityRouteError {
    fn from(err: ComponentTreeError) -> Self {
        CapabilityRouteError::ComponentNotFound(err)
    }
}
