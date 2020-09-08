// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod element_manager_server;
mod event_handler;

#[cfg(test)]
mod testing_utils;

use {
    anyhow::{Context as _, Error},
    element_management::{Element, ElementManager, ElementManagerError},
    fidl_fuchsia_session::{
        ElementControllerMarker, ElementControllerRequestStream, ElementSpec, ProposeElementError,
    },
    fuchsia_syslog::fx_log_err,
    futures::{
        channel::mpsc::{self, UnboundedReceiver, UnboundedSender},
        StreamExt,
    },
    rand::{distributions::Alphanumeric, thread_rng, Rng},
    std::rc::Rc,
};

pub use {
    element_manager_server::ElementManagerServer,
    event_handler::{ElementEventHandler, EventHandler},
};

/// The child collection to add elements to. This must match a collection name declared in
/// this session's CML file.
const ELEMENT_COLLECTION_NAME: &str = "elements";

/// A Struct which manages the running elements.ExposedServices
///
/// The ElementRepository provides a single location for managing elements while
/// allowing multiple connections to be served at one time.
///
/// The repository can spawn many servers which serve the fuchsia.session.ElementManager
/// protocol. Each of these servers can then handle multiple connections.
pub struct ElementRepository<T: ElementManager> {
    element_manager: Rc<T>,
    receiver: UnboundedReceiver<ElementEvent>,
    sender: UnboundedSender<ElementEvent>,
}

impl<T: ElementManager> ElementRepository<T> {
    /// Creates a new instance of the element respository
    ///
    /// The ElementRepository is created with Rc<ElementManager> to allow the
    /// element manager to be shared by the resulting servers in a lockless manner.
    ///
    /// The type erasure works in conjunction with the stateless nature of the
    /// ElementManager to be able to safely work with many servers.
    pub fn new(element_manager: Rc<T>) -> ElementRepository<T> {
        let (sender, receiver) = mpsc::unbounded();

        ElementRepository { element_manager, sender, receiver }
    }

    /// Runs the repository with a given handler.
    ///
    /// The handler is responsible for responding to incoming events and processing them.
    /// A single handler is used for one repository allowing it to be safely mutated.
    pub async fn run_with_handler<'a>(
        &mut self,
        handler: &'a mut impl EventHandler,
    ) -> Result<(), Error> {
        while let Some(event) = self.receiver.next().await {
            match event {
                ElementEvent::ElementAdded { element, element_controller_stream } => {
                    handler.add_element(element, element_controller_stream);
                }
                ElementEvent::Shutdown => {
                    handler.shutdown();
                    break;
                }
            }
        }
        Ok(())
    }

    /// Creates a new ElementManagerServer suitable for handling incoming connections.
    ///
    /// A repository can handle many servers. All servers will send events to the repository's
    /// event handling system.
    pub fn make_server(&self) -> ElementManagerServer<T> {
        ElementManagerServer::new(self.element_manager.clone(), self.sender.clone())
    }

    /// Stops the event loop handling incoming requests. After this method is called no more events
    /// will be processed
    pub fn shutdown(&self) -> Result<(), Error> {
        Ok(self
            .sender
            .unbounded_send(ElementEvent::Shutdown)
            .context("Unable to send Shutdown message")?)
    }
}

/// Encapsulates the logic for interacting with the ElementManager::launch_element method.
///
/// This method is located in this file instead of the ElementManagerServer so that elements
/// can be launched directly from the repository.
pub(crate) async fn propose_element<T: ElementManager>(
    element_manager: &Rc<T>,
    spec: ElementSpec,
    element_controller: Option<fidl::endpoints::ServerEnd<ElementControllerMarker>>,
) -> Result<(Element, Option<ElementControllerRequestStream>), ProposeElementError> {
    let mut child_name: String = thread_rng().sample_iter(&Alphanumeric).take(16).collect();
    child_name.make_ascii_lowercase();

    let proposal_result =
        element_manager.launch_element(spec, &child_name, ELEMENT_COLLECTION_NAME).await;
    map_launch_element_result(proposal_result, element_controller)
}

fn map_launch_element_result(
    result: Result<Element, ElementManagerError>,
    element_controller: Option<fidl::endpoints::ServerEnd<ElementControllerMarker>>,
) -> Result<(Element, Option<ElementControllerRequestStream>), ProposeElementError> {
    match result {
        Ok(element) => match element_controller {
            Some(element_controller) => match element_controller.into_stream() {
                Ok(stream) => Ok((element, Some(stream))),
                Err(_) => Err(ProposeElementError::Rejected),
            },

            None => Ok((element, None)),
        },
        Err(ElementManagerError::UrlMissing { .. }) => Err(ProposeElementError::NotFound),
        Err(err) => {
            fx_log_err!("failed to launch element: {:?}", err);
            Err(ProposeElementError::Rejected)
        }
    }
}

/// An enum representing events that the single repository instance will listen to.
pub(crate) enum ElementEvent {
    /// Indicates that an element has been added to the session.
    ElementAdded {
        element: Element,
        element_controller_stream: Option<ElementControllerRequestStream>,
    },

    /// Tells the repository it should shutdown and perform any cleanup.
    Shutdown,
}

impl std::fmt::Debug for ElementEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ElementEvent::ElementAdded { .. } => write!(f, "ElementAdded"),
            ElementEvent::Shutdown => write!(f, "Shutdown"),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::testing_utils::{init_logger, make_mock_element, CallCountEventHandler},
        super::*,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_component as fcomponent,
        fidl_fuchsia_session::ElementControllerMarker,
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn shutdown_sends_shutdown_message() {
        init_logger();
        let mut repo = ElementRepository::new_for_test();
        assert!(repo.shutdown().is_ok());

        let result = repo.receiver.next().await;

        match result.unwrap() {
            ElementEvent::Shutdown => (),
            _ => assert!(false),
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn shutdown_event_forwards_to_handler_and_ends_loop() -> Result<(), Error> {
        init_logger();
        let mut repo = ElementRepository::new_for_test();
        let mut handler = CallCountEventHandler::default();

        let sender = repo.sender.clone();
        fasync::Task::local(async move {
            sender.unbounded_send(ElementEvent::Shutdown).expect("failed to send event");
        })
        .detach();

        repo.run_with_handler(&mut handler).await?;
        assert_eq!(handler.shutdown_call_count, 1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn element_added_event_forwards_to_handler() -> Result<(), Error> {
        init_logger();
        let mut repo = ElementRepository::new_for_test();
        let mut handler = CallCountEventHandler::default();

        let (element, _channel) = make_mock_element();
        let sender = repo.sender.clone();
        fasync::Task::local(async move {
            sender
                .unbounded_send(ElementEvent::ElementAdded {
                    element,
                    element_controller_stream: None,
                })
                .expect("failed to send added event");

            // need to shut down the handler
            sender.unbounded_send(ElementEvent::Shutdown).expect("failed to send event");
        })
        .detach();

        repo.run_with_handler(&mut handler).await?;
        assert_eq!(handler.add_call_count, 1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn map_launch_element_result_url_missing() {
        init_logger();
        let error = ElementManagerError::url_missing("", "");
        let result = map_launch_element_result(Err(error), None);
        match result {
            Err(ProposeElementError::NotFound) => (),
            _ => panic!("wrong error returned"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn map_launch_element_result_not_created() {
        init_logger();
        let error = ElementManagerError::not_created("", "", "", fcomponent::Error::Internal);
        let result = map_launch_element_result(Err(error), None);
        match result {
            Err(ProposeElementError::Rejected) => (),
            _ => panic!("wrong error returned"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn map_launch_element_result_not_bound() {
        init_logger();
        let error = ElementManagerError::not_bound("", "", "", fcomponent::Error::Internal);
        let result = map_launch_element_result(Err(error), None);
        match result {
            Err(ProposeElementError::Rejected) => (),
            _ => panic!("wrong error returned"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn map_launch_element_result_not_launched() {
        init_logger();
        let error = ElementManagerError::not_launched("", "");
        let result = map_launch_element_result(Err(error), None);
        match result {
            Err(ProposeElementError::Rejected) => (),
            _ => panic!("wrong error returned"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn map_launch_element_result_just_element() {
        init_logger();
        let (element, _channel) = make_mock_element();
        let result = map_launch_element_result(Ok(element), None);
        match result {
            Ok((_, None)) => (),
            _ => panic!("should have gotten (element, None)"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn map_launch_element_result_element_and_controlller() {
        init_logger();
        let (_controller_proxy, server_end) =
            create_proxy::<ElementControllerMarker>().expect("failed to create endpoints");
        let (element, _channel) = make_mock_element();
        let result = map_launch_element_result(Ok(element), Some(server_end));
        match result {
            Ok((_, Some(_))) => (),
            _ => panic!("should have gotten (element, Some)"),
        }
    }
}
