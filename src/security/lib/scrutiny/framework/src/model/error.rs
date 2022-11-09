// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Error, Debug)]
pub enum ModelError {
    #[error("expected version: {} found version {}", expected, found)]
    ModelVersionIncompatible { expected: usize, found: usize },
    #[error("expected magic: {} found magic {}", expected, found)]
    ModelMagicIncompatible { expected: String, found: String },
    #[error("data collection not found in model: {}. description: {}", name, description)]
    ModelCollectionNotFound { name: String, description: String },
}

impl ModelError {
    pub fn model_version_incompatible(expected: usize, found: usize) -> ModelError {
        ModelError::ModelVersionIncompatible { expected, found }
    }

    pub fn model_magic_incompatible(
        expected: impl Into<String>,
        found: impl Into<String>,
    ) -> ModelError {
        ModelError::ModelMagicIncompatible { expected: expected.into(), found: found.into() }
    }

    pub fn model_collection_not_found(
        name: impl Into<String>,
        description: impl Into<String>,
    ) -> ModelError {
        ModelError::ModelCollectionNotFound { name: name.into(), description: description.into() }
    }
}
