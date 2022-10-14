// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_bluetooth::profile::{DataElement, DataElementConversionError};
use std::convert::TryFrom;
use tracing::info;

// Newtype for unparsed HID descriptors
#[derive(Debug, PartialEq)]
pub struct Descriptor(pub Vec<u8>);

#[derive(Debug, PartialEq)]
pub struct DescriptorList(pub Vec<Descriptor>);

impl TryFrom<DataElement> for DescriptorList {
    type Error = DataElementConversionError;
    fn try_from(src: DataElement) -> Result<DescriptorList, Self::Error> {
        info!("Read HID descriptors: {:?}", src);
        // TODO(fxb/96987) Implement HID descriptor SDP record parsing.
        Ok(DescriptorList(Vec::new()))
    }
}
