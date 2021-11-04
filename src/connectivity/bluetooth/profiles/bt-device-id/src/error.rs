// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_deviceid::{DeviceIdentificationRecord, DeviceReleaseNumber};
use fuchsia_zircon as zx;
use thiserror::Error;

/// Errors that occur during the operation of the Device Identification component.
#[derive(Error, Debug)]
pub enum Error {
    #[error("No DI records provided to advertise")]
    EmptyRequest,
    #[error("Multiple primary records were provided")]
    MultiplePrimaryRecords,
    #[error("Invalid Device ID Record: {:?}", .0)]
    InvalidRecord(DeviceIdentificationRecord),
    #[error("Invalid device release number (version): {:?}", .0)]
    InvalidVersion(DeviceReleaseNumber),
    #[error("Fuchsia Zircon Error: {:?}", .0)]
    Internal(#[from] zx::Status),
    #[error("Fidl Error: {}", .0)]
    Fidl(#[from] fidl::Error),
}

impl From<&DeviceIdentificationRecord> for Error {
    fn from(src: &DeviceIdentificationRecord) -> Error {
        Error::InvalidRecord(src.clone())
    }
}

impl From<&DeviceReleaseNumber> for Error {
    fn from(src: &DeviceReleaseNumber) -> Error {
        Error::InvalidVersion(src.clone())
    }
}
