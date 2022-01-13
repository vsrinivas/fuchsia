// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::{protocol::response::Response, request_builder::RequestParams};
use futures::future::LocalBoxFuture;
use futures::prelude::*;
use thiserror::Error;

/// This is the collection of Errors that can occur during the installation of an update.
///
/// This is a placeholder stub implementation.
///
#[derive(Debug, Error, Eq, PartialEq)]
pub enum StubInstallErrors {
    #[error("Stub Installer Failure")]
    Failed,
}

/// A stub implementation of the Install Plan.
///
pub struct StubPlan;

impl Plan for StubPlan {
    fn id(&self) -> String {
        String::new()
    }
}

/// The Installer is responsible for performing (or triggering) the installation of the update
/// that's referred to by the InstallPlan.
///
#[derive(Debug, Default)]
pub struct StubInstaller {
    pub should_fail: bool,
}

impl Installer for StubInstaller {
    type InstallPlan = StubPlan;
    type Error = StubInstallErrors;
    type InstallResult = ();

    /// Perform the installation as given by the install plan (as parsed form the Omaha server
    /// response).  If given, provide progress via the observer, and a final finished or Error
    /// indication via the Future.
    fn perform_install(
        &mut self,
        _install_plan: &StubPlan,
        _observer: Option<&dyn ProgressObserver>,
    ) -> LocalBoxFuture<'_, (Self::InstallResult, Vec<AppInstallResult<Self::Error>>)> {
        if self.should_fail {
            future::ready(((), vec![AppInstallResult::Failed(StubInstallErrors::Failed)]))
                .boxed_local()
        } else {
            future::ready(((), vec![AppInstallResult::Installed])).boxed_local()
        }
    }

    fn perform_reboot(&mut self) -> LocalBoxFuture<'_, Result<(), anyhow::Error>> {
        future::ready(Ok(())).boxed_local()
    }

    fn try_create_install_plan<'a>(
        &'a self,
        _request_params: &'a RequestParams,
        response: &'a Response,
    ) -> LocalBoxFuture<'a, Result<Self::InstallPlan, Self::Error>> {
        if response.protocol_version != "3.0" {
            future::ready(Err(StubInstallErrors::Failed)).boxed_local()
        } else {
            future::ready(Ok(StubPlan)).boxed_local()
        }
    }
}
