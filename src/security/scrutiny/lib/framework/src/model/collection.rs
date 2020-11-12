// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use uuid::Uuid;

/// The DataCollection trait provides a way for Plugins to provide Data to
/// the data model. DataCollectors create DataCollections which are stored
/// in the DataModel. Plugins that depend on each other can use each others
/// DataCollections. This allows plugins to store any data they require
/// for analysis. All collections must have a way to serialize and deserialize
/// themselves in case they need to be stored.
pub trait DataCollection {
    /// The only requirement on a collection is that it provides a stable
    /// globally unique identifier. This should be baked into the source at
    /// compile time. The DataModel will use this information to uniquely
    /// identify the collection from other collections it has stored.
    fn uuid() -> Uuid
    where
        Self: Sized;
}
