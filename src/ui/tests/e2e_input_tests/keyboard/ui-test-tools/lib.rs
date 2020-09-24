// 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl::endpoints::{create_proxy, create_request_stream};
use fidl_fuchsia_ui_gfx as ui_gfx;
use fidl_fuchsia_ui_policy as ui_policy;
use fidl_fuchsia_ui_scenic as ui_scenic;
use fuchsia_component::client::connect_to_service;
use fuchsia_scenic::{EntityNode, Session, SessionPtr, View, ViewTokenPair};
use fuchsia_zircon::{ClockId, Time};
use futures::TryStreamExt;

/// Type of the e2e testing environment to setup.
pub enum EnvironmentType {
    /// Start Scenic and RootPresenter and wait for complete initialization.
    ScenicWithRootPresenter,
}

/// Provides setup and utility methods for UI e2e tests that use `Scenic` and `RootPresenter`.
pub struct Service {
    session: SessionPtr,
    presenter: ui_policy::PresenterProxy,
    #[allow(dead_code)]
    scenic: ui_scenic::ScenicProxy,
    session_listener_stream: ui_scenic::SessionListenerRequestStream,
}

impl Service {
    fn new(_environment: EnvironmentType) -> Result<Service, Error> {
        let presenter = connect_to_service::<ui_policy::PresenterMarker>()
            .context("connect to Presentation")?;

        let scenic =
            connect_to_service::<ui_scenic::ScenicMarker>().context("connect to Scenic")?;
        let (listener_client_end, session_listener_stream) =
            create_request_stream::<ui_scenic::SessionListenerMarker>()
                .context("create_request_stream")?;

        let (session_proxy, session_request) = create_proxy().context("create_proxy")?;
        scenic
            .create_session(session_request, Some(listener_client_end))
            .context("create_session")?;

        let session = Session::new(session_proxy);

        Ok(Service { presenter, scenic, session, session_listener_stream })
    }

    async fn present(&self) -> Result<(), Error> {
        let presentation_time = Time::get(ClockId::Monotonic).into_nanos() as u64;
        self.session.lock().present(presentation_time).await.context("session present")?;
        Ok(())
    }

    async fn view_holder_connected(&mut self) -> Result<(), Error> {
        while let Some(msg) =
            self.session_listener_stream.try_next().await.context("SessionListener event")?
        {
            match msg {
                ui_scenic::SessionListenerRequest::OnScenicEvent { events, .. } => {
                    for event in events.iter() {
                        if let ui_scenic::Event::Gfx(ui_gfx::Event::ViewHolderConnected(
                            ui_gfx::ViewHolderConnectedEvent { .. },
                        )) = event
                        {
                            return Ok(());
                        }
                    }
                }
                _ => {}
            }
        }
        Ok(())
    }
}

/// Setup UI e2e testing environment and wait for full setup of dependent services.
/// Resolves when the setup is complete, returning an instance of `Service`.
/// When instance of returned `Service` goes out of scope, dependent FIDL services will
/// be disconnected.
pub async fn setup(environment: EnvironmentType) -> Result<Service, Error> {
    let mut service = Service::new(environment)?;

    // Add a View and a root Entity as a child.
    let mut token_pair = ViewTokenPair::new()?;
    service
        .presenter
        .present_view(&mut token_pair.view_holder_token, None)
        .context("present_view")?;

    let root_node = EntityNode::new(service.session.clone());

    let view =
        View::new(service.session.clone(), token_pair.view_token, Some(String::from("root view")));
    view.add_child(&root_node);

    service.present().await?;
    // Wait for the ViewHolderConnected Scenic event to ensure RootPresenter is up and running.
    service.view_holder_connected().await.context("view_holder_connected")?;

    // Second present is required because of http://fxbug.dev/42737
    service.present().await?;

    Ok(service)
}
