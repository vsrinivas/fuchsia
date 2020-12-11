// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module contains an AST for AT command responses.
/// The format of these is not specifed in any one place in the spec, but they are
/// described thoughout HFP 1.8.
use crate::lowlevel::arguments;

/// Responses and indications.
#[derive(Debug, Clone, PartialEq)]
pub enum Response {
    /// The success indication.  Its format is described in HFP v1.8 Section 4.34.1, and it is
    /// used throughout the spec.
    Ok,
    /// The error indication.  Its format is described in HFP v1.8 Section 4.34.1, and it is
    /// used throughout the spec.
    Error,
    /// A set of hardcoded specific error indications.  They are described in HFP v1.8 Section 4.34.2.
    HardcodedError(HardcodedError),
    /// Error codes used with the +CME ERROR indication.  Described in HFP v1.8 4.34.2
    CmeError(i64),
    /// All other non-error responses.  These are described throughout the HFP v1.8 spec.
    Success { name: String, is_extension: bool, arguments: arguments::Arguments },
}

/// Hardedcoded error indicatons described in HFP v1.8 Section 4.34.2
#[derive(Debug, Clone, PartialEq)]
pub enum HardcodedError {
    NoCarrier,
    Busy,
    NoAnswer,
    Delayed,
    Blacklist,
}
