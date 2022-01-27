// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_ui_pointerinjector as pointerinjector,
    fidl_fuchsia_ui_pointerinjector_configuration::{
        SetupRequest, SetupRequestStream, SetupWatchViewportResponder,
    },
    fuchsia_component_test::new::{ChildOptions, RealmBuilder},
    futures::TryStreamExt,
};

/// A mock implementation of `fuchsia.ui.pointerinjector.configuration.Setup`, which
/// a) responds to every `GetViewRefs` request with a new `(context, target)`
///    pair, and
/// b) responds to the first `WatchViewport` request immediately, and
/// c) lets the second 'WatchViewport' hang indefinitely.
#[derive(Clone)]
pub(crate) struct PointerInjectorMock {
    name: String,
    viewport: pointerinjector::Viewport,
}

impl PointerInjectorMock {
    pub(crate) fn new<M: Into<String>>(name: M, viewport: pointerinjector::Viewport) -> Self {
        Self { name: name.into(), viewport }
    }

    async fn serve_one_client(self, mut request_stream: SetupRequestStream) {
        enum WatchState {
            WaitingFirstRequest(pointerinjector::Viewport),
            WaitingSecondRequest,
            GotSecondRequest(SetupWatchViewportResponder),
        }
        let mut watch_state = WatchState::WaitingFirstRequest(self.viewport.clone());
        let injection_context =
            fuchsia_scenic::ViewRefPair::new().expect("Failed to create viewrefpair.").view_ref;
        let injection_target =
            fuchsia_scenic::ViewRefPair::new().expect("Failed to create viewrefpair.").view_ref;
        while let Some(request) =
            request_stream.try_next().await.expect("Failed to read SetupRequest")
        {
            match request {
                SetupRequest::GetViewRefs { responder, .. } => {
                    responder
                        .send(
                            &mut fuchsia_scenic::duplicate_view_ref(&injection_context)
                                .expect("Failed to duplicate viewref"),
                            &mut fuchsia_scenic::duplicate_view_ref(&injection_target)
                                .expect("Failed to duplicate viewref"),
                        )
                        .expect("Failed to send view refs");
                }
                SetupRequest::WatchViewport { responder, .. } => {
                    watch_state = match watch_state {
                        WatchState::WaitingFirstRequest(viewport) => {
                            responder.send(viewport).expect("Failed to send viewport.");
                            WatchState::WaitingSecondRequest
                        }
                        WatchState::WaitingSecondRequest => {
                            // Save the responder, to prevent causing errors on the client side.
                            WatchState::GotSecondRequest(responder)
                        }
                        WatchState::GotSecondRequest(_) => {
                            panic!("Client sent WatchViewport when a request was already pending.")
                        }
                    }
                }
            }
        }
    }
}

impl_test_realm_component!(PointerInjectorMock);
