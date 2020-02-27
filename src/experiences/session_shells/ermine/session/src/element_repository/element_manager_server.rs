// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::element_repository::{propose_element, ElementEvent},
    anyhow::{Context as _, Error},
    element_management::ElementManager,
    fidl_fuchsia_session::{ElementManagerRequest, ElementManagerRequestStream},
    futures::{channel::mpsc::UnboundedSender, TryStreamExt},
    std::rc::Rc,
};

/// A struct which can handle many incoming requests.
///
/// The ElementManagerServer is a stateless struct which can handle many
/// incoming request streams.
pub struct ElementManagerServer<T: ElementManager> {
    element_manager: Rc<T>,
    sender: UnboundedSender<ElementEvent>,
}

impl<T: ElementManager> ElementManagerServer<T> {
    /// Creates a new instance of the ElementManagerServer.
    ///
    /// This method is intended to be called by the ElementRespository. To create a server,
    /// first, create an ElementRepository and then call ElementRepository::make_server.
    pub(crate) fn new(element_manager: Rc<T>, sender: UnboundedSender<ElementEvent>) -> Self {
        ElementManagerServer { element_manager, sender }
    }

    /// Handles the incoming ElementManagerRequestStream.
    ///
    /// This method will end when the request stream is closed. If the stream closes with an
    /// error the error will be returned in the Result.
    pub async fn handle_request(&self, stream: ElementManagerRequestStream) -> Result<(), Error> {
        ElementManagerServer::handle_request_stream(
            self.sender.clone(),
            self.element_manager.clone(),
            stream,
        )
        .await?;
        Ok(())
    }

    /// Handles the request stream without capturing the ElementManagerServer.
    async fn handle_request_stream<'a>(
        sender: UnboundedSender<ElementEvent>,
        element_manager: Rc<T>,
        mut stream: ElementManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            stream.try_next().await.context("Error handling element manager request stream")?
        {
            match request {
                ElementManagerRequest::ProposeElement { spec, element_controller, responder } => {
                    let mut result =
                        match propose_element(&element_manager, spec, element_controller).await {
                            Ok((element, element_controller_stream)) => {
                                let _ = sender.unbounded_send(ElementEvent::ElementAdded {
                                    element,
                                    element_controller_stream,
                                });
                                Ok(())
                            }
                            Err(e) => Err(e),
                        };

                    let _ = responder.send(&mut result);
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::element_repository::testing_utils::init_logger,
        anyhow::anyhow,
        fidl::encoding::Decodable,
        fidl::endpoints::{create_proxy, create_proxy_and_stream},
        fidl_fuchsia_session::{ElementControllerMarker, ElementManagerMarker, ElementSpec},
        fuchsia_async::{self as fasync, DurationExt, Timer},
        fuchsia_zircon::DurationNum,
        futures::{channel::mpsc, future::Either, FutureExt, StreamExt},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_propose_valid_element_no_controller() -> Result<(), Error> {
        init_logger();
        let (server, mut receiver) = ElementManagerServer::new_for_test();
        let (proxy, stream) =
            create_proxy_and_stream::<ElementManagerMarker>().expect("should make proxy/stream");

        let spec =
            ElementSpec { component_url: Some("foo".to_string()), ..ElementSpec::new_empty() };
        fasync::spawn_local(async move {
            let _ = proxy.propose_element(spec, None).await;
            drop(proxy);
        });
        let _ = server.handle_request(stream).await;

        match wait_for_first_event_or_timeout(&mut receiver).await {
            Ok(ElementEvent::ElementAdded { element: _, element_controller_stream: None }) => (),
            _ => panic!("did not get valid event"),
        }

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_propose_valid_element_with_controller() -> Result<(), Error> {
        init_logger();
        let (server, mut receiver) = ElementManagerServer::new_for_test();
        let (proxy, stream) =
            create_proxy_and_stream::<ElementManagerMarker>().expect("should make proxy/stream");

        let spec =
            ElementSpec { component_url: Some("foo".to_string()), ..ElementSpec::new_empty() };
        fasync::spawn_local(async move {
            let (_controller_proxy, server_end) =
                create_proxy::<ElementControllerMarker>().expect("failed to create endpoints");
            let _ = proxy.propose_element(spec, Some(server_end)).await;
            drop(proxy);
        });
        let _ = server.handle_request(stream).await;

        match wait_for_first_event_or_timeout(&mut receiver).await {
            Ok(ElementEvent::ElementAdded { element: _, element_controller_stream: Some(_) }) => (),
            _ => panic!("did not get valid event"),
        }

        Ok(())
    }

    async fn wait_for_first_event_or_timeout(
        receiver: &mut mpsc::UnboundedReceiver<ElementEvent>,
    ) -> Result<ElementEvent, Error> {
        let timeout = Timer::new(500_i64.millis().after_now());
        let either = futures::future::select(timeout, receiver.next().fuse());
        let resolved = either.await;
        match resolved {
            Either::Left(_) => Err(anyhow!("wait_for_first_event_or_timeout timed out")),
            Either::Right((result, _)) => {
                let event = result.expect("result should not be None");
                Ok(event)
            }
        }
    }
}
