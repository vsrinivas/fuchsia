// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{protocol::response::Response, request_builder::RequestParams};
use futures::future::BoxFuture;

pub mod stub;

/// The trait for the Install Plan that can be acted on by an Installer implementation.
///
pub trait Plan: std::marker::Sized {
    type Error: std::error::Error + std::marker::Send + std::marker::Sync + 'static;

    /// Try to create a new Plan from the given response, returning a PlanError if unable to do
    /// so.
    fn try_create_from(
        request_params: &RequestParams,
        response: &Response,
    ) -> Result<Self, Self::Error>;

    /// A string that can identify individual install plans, used to check if the current plan is
    /// the same as the previous one.
    fn id(&self) -> String;
}

/// The trait for the platform-specific Installer to implement.
///
/// The Installer trait has two associated types:
///
/// InstallPlan - This is the type that implements the Plan trait, and represents the platform-
///               specific installation plan (the data used to define what an update is).
///
/// Error - This the type that implements the thiserror::Error trait and is used to collect all of
///         the errors that can occur during the installation of an update.
pub trait Installer {
    type InstallPlan: Plan;
    type Error: std::error::Error + std::marker::Send + std::marker::Sync + 'static;

    /// Perform the installation as given by the install plan (as parsed form the Omaha server
    /// response).  If given, provide progress via the observer, and a final finished or Error
    /// indication via the Future.
    fn perform_install(
        &mut self,
        install_plan: &Self::InstallPlan,
        observer: Option<&dyn ProgressObserver>,
    ) -> BoxFuture<'_, Result<(), Self::Error>>;
}

/// The trait for observing progress on the initiated installation.
///
/// The StateMachine may pass an implementation of this trait to the Installer, so that it can
/// receive reports of the progress of the installation of the update.
///
pub trait ProgressObserver {
    /// Receive progress on the installation.
    ///
    /// operation - The current operation of the install (if applicable)
    /// progress - 0 to 100, current percentage
    /// total_size - Maximal size of the download of the install
    /// size_so_far - Downloaded data so far (may move forward erratically based on cached or
    ///               previously downloaded data)
    fn receive_progress(
        &mut self,
        operation: Option<&str>,
        progress: u32,
        total_size: Option<u64>,
        size_so_far: Option<u64>,
    );
}
