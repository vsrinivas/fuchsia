// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Inspect Format
//!
//! This module provides utilities for formatting a `NodeHierarchy`.
//! Currently the only available format is JSON.
//!
//! ## JSON Example
//!
//! If you'd like to format a single hierarchy
//!
//! ```
//! let hierarchy = NodeHierarchy::new(...);
//! let json_string = serde_json::to_string(&hierarchy)?;
//! ```
//!
//! If you'd like to deserialize some json string, you can do the following:
//! ```
//! let string = "{ ... }".to_string();
//! let hierarchy : NodeHierarchy = serde_json::from_str(&string);
//! ```

mod deserialize;
mod serialize;
