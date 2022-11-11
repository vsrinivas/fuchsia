// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod shortcuts;
mod wm;

#[cfg(test)]
mod tests;

use anyhow::Error;
use appkit::{Event, EventSender, ProductionProtocolConnector, SystemEvent, ViewSpecHolder};
use fidl_fuchsia_element as felement;
use fidl_fuchsia_ui_app as ui_app;
use fuchsia_async as fasync;
use fuchsia_component::server;
use futures::{StreamExt, TryStreamExt};
use tracing::*;

use crate::wm::{get_first_view_creation_token, WMEvent, WindowManager};

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

const MAX_CONCURRENT: usize = 100;

enum IncomingService {
    GraphicalPresenter(felement::GraphicalPresenterRequestStream),
    ViewProvider(ui_app::ViewProviderRequestStream),
}

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), Error> {
    info!("{}", WELCOME_LOG);

    // Create an event sender receiver pair.
    let (sender, mut receiver) = EventSender::<WMEvent>::new();

    // Start listening to incoming services.
    start_services(sender.clone());

    // Get the first `view_creation_token` from incoming ViewProvider::CreateView2 request.
    let (view_creation_token, view_spec_holders) =
        get_first_view_creation_token(&mut receiver).await;

    // Create WindowManager instance and start handling events.
    let protocol_connector = Box::new(ProductionProtocolConnector());
    let mut wm =
        WindowManager::new(sender, view_creation_token, view_spec_holders, protocol_connector)?;
    wm.run(receiver).await?;

    info!("Shutdown");

    Ok(())
}

fn start_services(sender: EventSender<WMEvent>) {
    fasync::Task::local(async move {
        let mut fs = server::ServiceFs::new();
        fs.dir("svc").add_fidl_service(IncomingService::GraphicalPresenter);
        fs.dir("svc").add_fidl_service(IncomingService::ViewProvider);
        fs.take_and_serve_directory_handle().expect("Failed to start ServiceFs");

        fs.for_each_concurrent(MAX_CONCURRENT, |request| async {
            if let Err(err) = match request {
                IncomingService::ViewProvider(request_stream) => {
                    serve_view_provider(request_stream, sender.clone()).await
                }
                IncomingService::GraphicalPresenter(request_stream) => {
                    serve_graphical_presenter(request_stream, sender.clone()).await
                }
            } {
                tracing::error!("{:?}", err);
            }
        })
        .await;
    })
    .detach();
}

async fn serve_view_provider(
    mut request_stream: ui_app::ViewProviderRequestStream,
    sender: EventSender<WMEvent>,
) -> Result<(), Error> {
    while let Some(request) = request_stream.next().await {
        match request {
            Ok(ui_app::ViewProviderRequest::CreateView2 { args, .. }) => {
                if let Some(view_creation_token) = args.view_creation_token {
                    sender
                        .send(Event::SystemEvent {
                            event: SystemEvent::ViewCreationToken { token: view_creation_token },
                        })
                        .expect("failed to send SystemEvent::ViewCreationToken");
                } else {
                    error!("CreateView2() missing view_creation_token field");
                }
            }
            Ok(_) => error!("ViewProvider impl only handles CreateView2"),
            Err(e) => error!("Failed to read request from ViewProvider: {:?}", e),
        }
    }
    Ok(())
}

async fn serve_graphical_presenter(
    mut request_stream: felement::GraphicalPresenterRequestStream,
    sender: EventSender<WMEvent>,
) -> Result<(), Error> {
    while let Ok(Some(request)) = request_stream.try_next().await {
        match request {
            felement::GraphicalPresenterRequest::PresentView {
                view_spec,
                annotation_controller,
                view_controller_request,
                responder,
            } => {
                sender
                    .send(Event::SystemEvent {
                        event: SystemEvent::PresentViewSpec {
                            view_spec_holder: ViewSpecHolder {
                                view_spec,
                                annotation_controller,
                                view_controller_request,
                                responder: Some(responder),
                            },
                        },
                    })
                    .expect("failed to send SystemEvent::PresentViewSpec");
            }
        }
    }
    Ok(())
}
