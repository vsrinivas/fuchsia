// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {std::error::Error as StdError, thiserror::Error};

/// Errors that occur during the operation of the HFP Bluetooth Profile component.
#[derive(Error, Debug)]
pub enum Error {
    #[error("Error using BR/EDR resource {:?}", .resource)]
    ProfileResourceError { resource: ProfileResource, source: Box<dyn StdError> },
}

#[derive(Debug)]
pub enum ProfileResource {
    SearchResults,
    ConnectionReceiver,
    Advertise,
}

#[derive(Debug, Error)]
#[error("Advertisement Terminated")]
pub struct AdvertisementTerminated;

impl Error {
    /// Make a new ProfileResourceError
    fn profile_resource<E: StdError + 'static>(resource: ProfileResource, e: E) -> Self {
        Error::ProfileResourceError { resource, source: Box::new(e) }
    }

    /// An error occurred when attempting to register an advertisement.
    pub fn profile_advertise<E: StdError + 'static>(e: E) -> Self {
        Self::profile_resource(ProfileResource::Advertise, e)
    }

    /// An error occurred when attempting to use the fuchsia.bluetooth.bredr.SearchResults fidl
    /// protocol.
    pub fn profile_search_results<E: StdError + 'static>(e: E) -> Self {
        Self::profile_resource(ProfileResource::SearchResults, e)
    }

    /// An error occurred when attempting to use the fuchsia.bluetooth.bredr.ConnectionReceiver fidl
    /// protocol.
    pub fn profile_connection_receiver<E: StdError + 'static>(e: E) -> Self {
        Self::profile_resource(ProfileResource::ConnectionReceiver, e)
    }
}
