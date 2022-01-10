// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Error, Debug)]
pub enum StoreError {
    #[error("collection: {} already exists in this database.", collection)]
    CollectionAlreadyExists { collection: String },
    #[error("collection: {} does not exist in the database.", collection)]
    CollectionDoesNotExist { collection: String },
}

impl StoreError {
    pub fn collection_already_exists(collection: impl Into<String>) -> StoreError {
        StoreError::CollectionAlreadyExists { collection: collection.into() }
    }

    pub fn collection_does_not_exist(collection: impl Into<String>) -> StoreError {
        StoreError::CollectionDoesNotExist { collection: collection.into() }
    }
}

#[derive(Error, Debug)]
pub enum CollectionError {
    #[error("collection: element {} does not exist.", key)]
    ElementNotFound { key: String },
}

impl CollectionError {
    pub fn element_not_found(key: String) -> CollectionError {
        CollectionError::ElementNotFound { key: key }
    }
}
