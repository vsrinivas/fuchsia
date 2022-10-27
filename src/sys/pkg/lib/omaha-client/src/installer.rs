// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    cup_ecdsa::RequestMetadata, protocol::response::Response, request_builder::RequestParams,
};
use futures::future::{BoxFuture, LocalBoxFuture};

pub mod stub;

/// The trait for the Install Plan that can be acted on by an Installer implementation.
///
pub trait Plan: std::marker::Sized + std::marker::Sync {
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
/// InstallResult - InstallResult is data passed from the Installer trait implementation to the PolicyEngine
///                 trait implementation to help the PolicyEngine schedule the reboot.
///
/// Error - This the type that implements the thiserror::Error trait and is used to collect all of
///         the errors that can occur during the installation of an update.
pub trait Installer {
    type InstallPlan: Plan;
    type InstallResult;
    type Error: std::error::Error + std::marker::Send + std::marker::Sync + 'static;

    /// Perform the installation as given by the install plan (as parsed form the Omaha server
    /// response).  If given, provide progress via the observer, and a final finished or Error
    /// indication via the Future.
    /// The returned Vec of AppInstallResult must only include apps that have an update available
    /// and must be kept in the same order as it appears in the omaha response.
    #[allow(clippy::type_complexity)]
    fn perform_install<'a>(
        &'a mut self,
        install_plan: &'a Self::InstallPlan,
        observer: Option<&'a dyn ProgressObserver>,
    ) -> LocalBoxFuture<'a, (Self::InstallResult, Vec<AppInstallResult<Self::Error>>)>;

    /// Perform a reboot of the system (in whichever manner that the installer needs to perform
    /// a reboot.  This fn should not return unless reboot failed.
    fn perform_reboot(&mut self) -> LocalBoxFuture<'_, Result<(), anyhow::Error>>;

    /// Try to create a new Plan from the given response, returning a Error if unable to do so.
    /// For update with multiple apps, the install plan must keep the order of the apps from the
    /// response.
    fn try_create_install_plan<'a>(
        &'a self,
        request_params: &'a RequestParams,
        request_metadata: Option<&'a RequestMetadata>,
        response: &'a Response,
        response_bytes: Vec<u8>,
        ecdsa_signature: Option<Vec<u8>>,
    ) -> LocalBoxFuture<'a, Result<Self::InstallPlan, Self::Error>>;
}

#[derive(Debug)]
pub enum AppInstallResult<E> {
    Installed,
    Deferred,
    Failed(E),
}

impl<E> From<Result<(), E>> for AppInstallResult<E> {
    fn from(result: Result<(), E>) -> Self {
        match result {
            Ok(()) => Self::Installed,
            Err(e) => Self::Failed(e),
        }
    }
}

/// The trait for observing progress on the initiated installation.
///
/// The StateMachine may pass an implementation of this trait to the Installer, so that it can
/// receive reports of the progress of the installation of the update.
///
pub trait ProgressObserver: Sync {
    /// Receive progress on the installation.
    ///
    /// operation - The current operation of the install (if applicable)
    /// progress - 0 to 1 fraction completed.
    /// total_size - Maximal size of the download of the install
    /// size_so_far - Downloaded data so far (may move forward erratically based on cached or
    ///               previously downloaded data)
    fn receive_progress(
        &self,
        operation: Option<&str>,
        progress: f32,
        total_size: Option<u64>,
        size_so_far: Option<u64>,
    ) -> BoxFuture<'_, ()>;
}
