// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::base::SettingHandlerResult;
use crate::internal::core;
use crate::policy::base::response::Response;
use crate::policy::base::Request;
use crate::switchboard::base::SettingRequest;
use async_trait::async_trait;

/// PolicyHandlers are in charge of applying and persisting policies set by clients.
#[async_trait]
pub trait PolicyHandler {
    /// Called when a policy client makes a request on the policy API this handler controls.
    async fn handle_policy_request(&mut self, request: Request) -> Response;

    /// Called when a setting request is intercepted for the setting this policy handler supervises.
    ///
    /// If there are no policies or the request does not need to be modified, `None` should be
    /// returned.
    ///
    /// If this handler wants to consume the request and respond to the client directly, it should
    /// return [`Transform::Result`].
    ///
    /// If this handler wants to modify the request, then let the setting handler handle it,
    /// [`Transform::Request`] should be returned, with the modified request.
    ///
    /// [`Transform::Result`]: enum.Transform.html
    /// [`Transform::Request`]: enum.Transform.html
    async fn handle_setting_request(
        &mut self,
        request: SettingRequest,
        messenger: core::message::Messenger,
    ) -> Option<Transform>;
}

/// `Transform` is returned by a [`PolicyHandler`] in response to a setting request that a
/// [`PolicyProxy`] intercepted. The presence of this value indicates that the policy handler has
/// decided to take action in order to apply policies.
///
/// [`PolicyHandler`]: trait.PolicyHandler.html
/// [`PolicyProxy`]: ../policy_proxy/struct.PolicyProxy.html
///
// TODO(fxbug.dev/59747): remove when used
#[allow(dead_code)]
#[derive(Clone, Debug, PartialEq)]
pub enum Transform {
    /// A new, modified request that should be forwarded to the setting handler for processing.
    Request(SettingRequest),

    /// A result to return directly to the settings client.
    Result(SettingHandlerResult),
}
