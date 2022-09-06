// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    fidl_fuchsia_element as felement, fidl_fuchsia_session_scene as fscene,
    fidl_fuchsia_ui_composition as ui_comp,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_scenic::flatland,
    futures::{stream, StreamExt},
};

mod wm;

enum IncomingService {
    GraphicalPresenter(felement::GraphicalPresenterRequestStream),
}

#[fuchsia::main(logging = true)]
async fn main() -> anyhow::Result<()> {
    let flatland = connect_to_protocol::<flatland::FlatlandMarker>()?;
    let scene_manager = connect_to_protocol::<fscene::ManagerMarker>()?;

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(IncomingService::GraphicalPresenter);
    fs.take_and_serve_directory_handle()?;

    let mut view_creation_token_pair = flatland::ViewCreationTokenPair::new()?;

    // This future can only be polled after the first call to `present`.
    let present_root_result =
        scene_manager.present_root_view(&mut view_creation_token_pair.viewport_creation_token);

    let mut flatland_events = flatland.take_event_stream();
    let mut incoming_connections = fs.fuse();

    let mut graphical_presenter_requests = stream::SelectAll::new();
    let mut events = stream::SelectAll::new();

    let mut server =
        wm::WindowManager::new(flatland.clone(), view_creation_token_pair.view_creation_token)
            .await?;

    flatland.present(flatland::PresentArgs::EMPTY)?;

    present_root_result
        .await
        .context("presenting root view")?
        .map_err(|err| anyhow!("presenting root view err: {:?}", err))?;

    let mut presentation_budget = 0;
    let mut presentation_requested = false;

    loop {
        // Present if we want to and have the budget for it.
        if presentation_requested && 0 < presentation_budget {
            flatland.present(flatland::PresentArgs::EMPTY)?;
            presentation_budget -= 1;
            presentation_requested = false;
        }

        futures::select! {
            flatland_event = flatland_events.select_next_some() =>
                match flatland_event.context("from flatland events")? {
                    flatland::FlatlandEvent::OnNextFrameBegin {
                        values: ui_comp::OnNextFrameBeginValues{
                            additional_present_credits, ..}
                        } => {
                            if let Some(delta) = additional_present_credits {
                                presentation_budget += delta;
                            }
                        }
                    flatland::FlatlandEvent::OnFramePresented { .. } => {},
                    flatland::FlatlandEvent::OnError { error } => {
                        return Err(anyhow::anyhow!("flatland error: {:?}", error))
                    }
                },
            connection_request = incoming_connections.select_next_some() =>
                match connection_request {
                    IncomingService::GraphicalPresenter(stream) =>
                        graphical_presenter_requests.push(stream),
                },
            request = graphical_presenter_requests.select_next_some() => {
                let present_view_request = request.context("getting PresentView request")?;
                events.push(server.present_view(present_view_request)?);
                presentation_requested = true;
            },
            event = events.select_next_some() => {
                events.push(server.handle_event(event));
                presentation_requested = true;
            }
        }
    }
}
