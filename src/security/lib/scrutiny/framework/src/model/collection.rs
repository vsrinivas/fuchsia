// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The DataCollection trait provides a way for Plugins to provide Data to
/// the data model. DataCollectors create DataCollections which are stored
/// in the DataModel. Plugins that depend on each other can use each others
/// DataCollections. This allows plugins to store any data they require
/// for analysis in memory.
pub trait DataCollection {
    /// A name for the collection.
    fn collection_name() -> String;
    /// A description of what this collection contains.
    fn collection_description() -> String;
}
