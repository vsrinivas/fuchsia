// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Response types.  These wrap the generated Success types with handling for errors and other types of responses.

use crate::highlevel;

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
    Success(highlevel::Success),
    /// Just send the raw byte buffer as a response.
    RawBytes(Vec<u8>),
}

// Hardedcoded error indications described in HFP v1.8 Section 4.34.2
#[derive(Debug, Clone, PartialEq)]
pub enum HardcodedError {
    NoCarrier,
    Busy,
    NoAnswer,
    Delayed,
    Blacklist,
}
