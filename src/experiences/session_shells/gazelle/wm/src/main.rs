// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use fidl_fuchsia_element as felement;
use fidl_fuchsia_ui_app as ui_app;
use fidl_fuchsia_ui_composition as ui_comp;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_component::{client, server};
use fuchsia_scenic::flatland;
use futures::{stream, StreamExt};

mod wm;

// A fun picture to print to the log when gazelle launches. This is derived from
// a drawing in the public domain, found at
// https://www.rawpixel.com/image/6502476/png-sticker-vintage.
const WELCOME_LOG: &'static str = "
             ▒▒░          ░░▒
             ▒▒▒          ░▒░
             ░▒▒░        ▒▒▒░          GAZELLE
              ▒▒▒░      ▒▒▒
              ░▓▒░     ▒▒▓░     ░░░░
   ░░░░        ▓▒▒    ░▒▒▒     ░░░░▒░
   ░▒░░░░      ▒▒▒░  ░▒▒▓   ░░▒░░░░░░
    ░░ ░▒▒▒░   ▓▒▒░  ▒▒▒▒  ░▒▒▒▒░░░
      ░░▒▒▓▒▒░░▓▓▓▒░▒▓▓▒▒▒▒▒░▒▒▒░░░
       ░░░▒▒░▒▒▓▓▓▓▓▓▓▓░░▓░░▒▒░░
          ░▒▒▒░░▓▓▒▓▓▒░  ▒░▒░░
            ░▓▓░▒▓▓▓░ ░▒▓▓▒░
             ▒▒▒░▓▓▒  ░▒▒▓▒
              ▒▒▓▓▒▒ ▒▒░▒▓▒░
              ░▒▓▒▒▒▒▒░▒▓▓░▒░
               ▒▒▒░░▒▒▓▓▓▓▒▒░
              ░▓▓▓▒░░▓▓▓▓▓▒▒▒                    ░░░    ░░░░░
               ▒▓▓▒▒▓▓▒▒▒▓▒░▒░  ░░░     ░░░░░░░░▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░
                ▒▓▓▓▓▓▒░▒▓▒▒░▓▒▒▒▒▒▒▒▒▒▒▒▒▓▒▒▒▒▒░░░░▒▒░░░░░▒▒▒▒▓▓▒▒▒░░░░░
                ░▓▓▓▓▓▒░▒▓▓░▒▒▓▒▒▒░░░▒▒▒▒▒▒░░░░░░ ░░ ░░░░▒▒▒▒▒▒▒▒▒▒▒░▒▒▓▒░
                ░▓▓▓▓▓▒▒▒▓▓▒▒░▒▒▒▒▒░ ░░░▒▒░░░░░░░░░░    ░░▒▒▒▒▒░░░░▒░░░▒▒▒▒░
                 ▒▓▓▓▓▓▒░▒▒▒▒▒▒▒▒░░ ░░  ░▒▒░░░░░░░░░░░   ░░░░▒▒▒▒░░░▒░░▒▓▒▓░
                  ▒▓▓▓▓▒░░░▒▒▒▓▒░   ░░░░░▒▒░▒▒░░░░░░░░░░    ░▒▒▒▒░  ░▒░░▓▒▓░
                  ░▒▓▓▓▒▒░░░░░▒▒░░  ░░░░░▒░░░▒░░░░░░░░░░░░  ░▓▓▒░  ░░░░▒▓▒▓▒
                   ░▒▒▓▓▓▒▒░░░░▒░░ ░░  ░░▒░░░▒▒░░░░░░░░░░▒▒▒▓▓▓▒░░░░▒░░▒▓▒▓▓░
                     ░▓▓▓▓▒▒░░░▒▒░░░░░ ░░▒░░ ░▒▒▒▒▒▒▒▒▒▒▓▓▓▓▓▒▓▓░░░░▒░░▒▓░▒▓▒
                     ░▒▓▓▒▒▒▒▒░▒▒░ ░░░░░░░░░░░▓▓▓▓▓▓▓▓▓▓▓▓▓▒░░░▒░░░▒▒░▒▒▒   ░░
                      ░░▒▒▒▒▒▒▒▒▓▒░░▒░░░░░ ░▒▒▓▓▓▓▓▓▓▓▒▒▒░ ░▒▓▒▒▒░▒▒░░▒▓▒
                        ░▒▒░░  ░░▒▒▒▒▒░▒▒▒▒▒▒▓▓▒░░░░    ░░▒▓▓▓▓▒▒▓▓▒▒▒▓▓▒
                         ░░▒░░ ░░▒▒░▒▒░▒▒▒▓▓▓▓▓▒░░░░░░░░▒░▒▓▓▓▓▓▒▒▓▓▒▒▒▓▓▒░
                           ░░▒▒▒░░▒░▒▓▒▒░▒▒▓▓▓▓▒░░░░░░░░   ░▒▓▓▓▓▓▒▓▓▒▒▓▓▓▒░
                             ░░▓▓▒▒▒▒▒▒▒░░░▒▓▓▒░             ░▒▒▒▓▓▓▓▓▓▓▒▓▓▓▒
                               ▒▓▓▓▒▒    ░░▒▓▓▒                 ░▒▓▓▓▒▒▓▒▓▓▓▒
                               ▒▒▓▒▒░    ░▒▒▒▓▒                  ░░▓▒▒░▒░▒▒░▓";

enum IncomingService {
    GraphicalPresenter(felement::GraphicalPresenterRequestStream),
    ViewProvider(ui_app::ViewProviderRequestStream),
}

/// Reads incoming connection requests from the given Stream until it finds a
/// request to connect to a `ViewProvider`. Meanwhile, it queues up other
/// incoming connections in a Vec. It then returns both.
///
/// This panics if the stream closes without an incoming `ViewPresenter`
/// connection.
async fn first_view_provider_request_stream(
    connections: &mut (impl stream::Stream<Item = IncomingService> + Unpin),
) -> (ui_app::ViewProviderRequestStream, Vec<IncomingService>) {
    let mut queue = Vec::new();

    while let Some(connection) = connections.next().await {
        match connection {
            IncomingService::ViewProvider(stream) => return (stream, queue),
            other => queue.push(other),
        }
    }
    panic!("incoming connections stream closed without any ViewProvider connection")
}

/// Returns the `ViewCreationToken` and `ViewProviderControlHandle` from the
/// first request on the given `ViewProviderRequestStream`. Panics if the first
/// request isn't a call to `CreateView2` which passes a `ViewCreationToken`.
async fn get_create_view2_request(
    stream: &mut ui_app::ViewProviderRequestStream,
) -> (ui_views::ViewCreationToken, ui_app::ViewProviderControlHandle) {
    let first_request = stream
        .next()
        .await
        .expect("ViewProviderRequestStream was empty")
        .expect("reading from ViewProviderRequestStream");

    match first_request {
        ui_app::ViewProviderRequest::CreateView2 { args, control_handle } => (
            args.view_creation_token.expect("first request did not contain a ViewCreationToken"),
            control_handle,
        ),
        _ => panic!("Only CreateView2 is supported"),
    }
}

#[fuchsia::main(logging = true)]
async fn main() -> anyhow::Result<()> {
    tracing::info!("{}", WELCOME_LOG);
    let flatland = client::connect_to_protocol::<flatland::FlatlandMarker>()?;

    let mut fs = server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(IncomingService::GraphicalPresenter);
    fs.dir("svc").add_fidl_service(IncomingService::ViewProvider);
    fs.take_and_serve_directory_handle()?;

    // NOTE: `view_provider_request_stream` needs to stay alive or the caller
    // gets unhappy.
    let (mut view_provider_request_stream, queued_connections) =
        first_view_provider_request_stream(&mut fs).await;
    let (view_creation_token, _control_handle) =
        get_create_view2_request(&mut view_provider_request_stream).await;

    let mut flatland_events = flatland.take_event_stream();
    let mut incoming_connections = stream::iter(queued_connections.into_iter()).chain(fs).fuse();

    let mut graphical_presenter_requests = stream::SelectAll::new();

    let mut manager =
        wm::Manager::new(wm::View::new(flatland.clone(), view_creation_token)?).await?;
    flatland.present(flatland::PresentArgs::EMPTY)?;

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
                        _ => {
                            tracing::warn!("received a second attempt to connect to
                                ViewProvider. Ignoring it.")
                        }
                },
            request = graphical_presenter_requests.select_next_some() => {
                let felement::GraphicalPresenterRequest::PresentView {
                    view_spec,
                    annotation_controller,
                    view_controller_request,
                    responder,
                } = request.context("getting PresentView request")?;
                manager.present_view(
                    view_spec, annotation_controller, view_controller_request)?;

                responder.send(&mut Ok(())).context("while replying to PresentView")?;
                presentation_requested = true;
            },
            background_result = manager.select_background_task() => {
                let () = background_result.expect("while doing background work");
                presentation_requested = true;
             }
        }
    }
}
