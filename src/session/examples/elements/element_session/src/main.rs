// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    element_management::{Element, ElementManager, ElementManagerError, SimpleElementManager},
    fidl_fuchsia_session::{
        AnnotationError, ElementControllerRequest, ElementControllerRequestStream,
        ElementManagerRequest, ElementManagerRequestStream, ProposeElementError,
    },
    fidl_fuchsia_session_examples::{ElementPingRequest, ElementPingRequestStream},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    futures::{StreamExt, TryStreamExt},
    rand::{distributions::Alphanumeric, thread_rng, Rng},
};

/// This enum allows the session to match on incoming messages.
enum ExposedServices {
    ElementManager(ElementManagerRequestStream),
    ElementPing(ElementPingRequestStream),
}

/// The child collection to add elements to. This must match a collection name declared in
/// this session's CML file.
const ELEMENT_COLLECTION_NAME: &str = "elements";

/// The maximum number of concurrent requests.
const NUM_CONCURRENT_REQUESTS: usize = 5;

/// This session exposes one service which is offered to all elements started in the session and
/// prints a string when an element sends a request to said service.
///
/// It also exposes the [`fidl_fuchsia_session.ElementManager`] service which an element proposer
/// can connect to in order to add an element to the session.
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::ElementPing);
    fs.dir("svc").add_fidl_service(ExposedServices::ElementManager);

    fs.take_and_serve_directory_handle()?;

    fs.for_each_concurrent(
        NUM_CONCURRENT_REQUESTS,
        move |service_request: ExposedServices| async move {
            match service_request {
                ExposedServices::ElementPing(request_stream) => {
                    handle_element_ping_requests(request_stream)
                        .await
                        .expect("Failed to run element ping service.");
                }
                ExposedServices::ElementManager(request_stream) => {
                    handle_element_manager_requests(request_stream)
                        .await
                        .expect("Failed to run element manager service.");
                }
            }
        },
    )
    .await;
    Ok(())
}

/// Handles the ping requests and prints to the terminal on success.
///
/// # Parameters
/// `stream`: The input channel which receives [`Ping`] requests.
///
/// # Returns
/// `Ok` if the service ran successfully, or an `Error` if execution halted unexpectedly.
async fn handle_element_ping_requests(mut stream: ElementPingRequestStream) -> Result<(), Error> {
    while let Some(ElementPingRequest::Ping { control_handle: _ }) =
        stream.try_next().await.context("Error handling ping request stream")?
    {
        println!("Element did ping session.");
    }
    Ok(())
}

/// Handles the [`ElementManager`] requests and launches the element session on success.
///
/// # Parameters
/// `stream`: The input channel which receives [`ElementManager`] requests.
///
/// # Returns
/// `Ok` if the element manager ran successfully, or an `ElementManagerError` if execution halted unexpectedly.
async fn handle_element_manager_requests(
    mut stream: ElementManagerRequestStream,
) -> Result<(), Error> {
    let mut uncontrolled_elements = vec![];
    let realm =
        connect_to_service::<fsys::RealmMarker>().context("Could not connect to Realm service.")?;

    let element_manager = SimpleElementManager::new(realm);
    while let Some(request) =
        stream.try_next().await.context("Error handling element manager request stream")?
    {
        match request {
            ElementManagerRequest::ProposeElement { spec, element_controller, responder } => {
                let mut child_name: String =
                    thread_rng().sample_iter(&Alphanumeric).take(16).collect();
                child_name.make_ascii_lowercase();

                let mut result = match element_manager
                    .launch_element(spec, &child_name, ELEMENT_COLLECTION_NAME)
                    .await
                {
                    Ok(element) => {
                        match element_controller {
                            Some(element_controller) => match element_controller.into_stream() {
                                Ok(stream) => {
                                    fasync::Task::spawn(handle_element_controller_request_stream(
                                        stream, element,
                                    ))
                                    .detach();
                                    Ok(())
                                }
                                Err(_) => Err(ProposeElementError::Rejected),
                            },
                            // If the element proposer did not provide a controller, add the
                            // element to a vector to keep it alive:
                            None => {
                                uncontrolled_elements.push(element);
                                Ok(())
                            }
                        }
                    }
                    // Most of the errors which could be encountered when adding an element are
                    // not the result of an error by the FIDL client. This lists all the cases
                    // explicitly, but it's up to each session to decide how to map the errors.
                    Err(ElementManagerError::UrlMissing { .. }) => {
                        Err(ProposeElementError::NotFound)
                    }
                    Err(ElementManagerError::InvalidServiceList { .. }) => {
                        Err(ProposeElementError::Rejected)
                    }
                    Err(ElementManagerError::AdditionalServicesNotSupported { .. }) => {
                        Err(ProposeElementError::Rejected)
                    }
                    Err(ElementManagerError::NotCreated { .. }) => {
                        Err(ProposeElementError::Rejected)
                    }
                    Err(ElementManagerError::NotBound { .. }) => Err(ProposeElementError::Rejected),
                    Err(ElementManagerError::NotLaunched { .. }) => {
                        Err(ProposeElementError::Rejected)
                    }
                };

                let _ = responder.send(&mut result);
            }
        }
    }
    Ok(())
}

/// Handles the ElementController requests.
///
/// # Parameters
/// - `stream`: the input channel which receives [`ElementController`] requests.
/// - `element`: the [`Element`] that is being controlled.
///
/// # Returns
/// () when there are no more valid requests.
async fn handle_element_controller_request_stream(
    mut stream: ElementControllerRequestStream,
    mut element: Element,
) {
    while let Ok(Some(request)) = stream.try_next().await {
        match request {
            ElementControllerRequest::SetAnnotations { annotations, responder } => {
                let _ = responder.send(
                    &mut element
                        .set_annotations(annotations)
                        .map_err(|_: anyhow::Error| AnnotationError::Rejected),
                );
            }
            ElementControllerRequest::GetAnnotations { responder } => {
                let mut annotations = &mut element
                    .get_annotations()
                    .map_err(|_: anyhow::Error| AnnotationError::NotFound);
                let _ = responder.send(&mut annotations);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy_and_stream, fuchsia_async as fasync,
        fuchsia_zircon as zx,
    };

    /// Tests that an Element is created with an empty vector of Annotations.
    #[fasync::run_singlethreaded(test)]
    async fn get_annotations_test() {
        let url = "component_url";
        let collection = "collection";
        let name = "name";
        let (p1, _) = zx::Channel::create().unwrap();

        let element = Element::from_directory_channel(
            p1,
            &name.to_string(),
            &url.to_string(),
            &collection.to_string(),
        );
        let (element_controller_proxy, element_controller_server) =
            create_proxy_and_stream::<fidl_fuchsia_session::ElementControllerMarker>()
                .expect("Failed to create ElementController proxy and server.");

        fasync::Task::spawn(handle_element_controller_request_stream(
            element_controller_server,
            element,
        ))
        .detach();
        let annotations = element_controller_proxy.get_annotations().await;
        assert!(annotations.is_ok());
        let unwrapped_annotations = annotations.unwrap();
        assert!(unwrapped_annotations.is_ok());
        assert_eq!(unwrapped_annotations.unwrap().custom_annotations.unwrap().len(), 0);
    }

    /// Tests that get_annotations properly returns the Annotations passed in via set_annotations().
    #[fasync::run_singlethreaded(test)]
    async fn set_annotations_test() {
        let url = "component_url";
        let collection = "collection";
        let name = "name";
        let (p1, _) = zx::Channel::create().unwrap();

        let element = Element::from_directory_channel(
            p1,
            &name.to_string(),
            &url.to_string(),
            &collection.to_string(),
        );

        let (element_controller_proxy, element_controller_server) =
            create_proxy_and_stream::<fidl_fuchsia_session::ElementControllerMarker>()
                .expect("Failed to create ElementController proxy and server.");

        fasync::Task::local(handle_element_controller_request_stream(
            element_controller_server,
            element,
        ))
        .detach();

        let annotation = fidl_fuchsia_session::Annotation {
            key: "key".to_string(),
            value: Some(Box::new(fidl_fuchsia_session::Value::Text("value".to_string()))),
        };
        let annotations =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation]) };

        // Cannot implement a Copy of an Annotation so just make another variable with idenitical
        // fields to test the resulting calls.
        let annotation_2 = fidl_fuchsia_session::Annotation {
            key: "key".to_string(),
            value: Some(Box::new(fidl_fuchsia_session::Value::Text("value".to_string()))),
        };
        let annotations_2 =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation_2]) };
        let _ = element_controller_proxy.set_annotations(annotations).await.unwrap();

        let returned_annotations = element_controller_proxy.get_annotations().await;
        assert!(returned_annotations.is_ok());
        let unwrapped_annotations = returned_annotations.unwrap();
        assert!(unwrapped_annotations.is_ok());
        assert_eq!(unwrapped_annotations.unwrap(), annotations_2);
    }

    /// Tests that calling set_annotations() twice with the same key updates the value as expected.
    #[fasync::run_singlethreaded(test)]
    async fn update_annotations_test() {
        let url = "component_url";
        let collection = "collection";
        let name = "name";
        let (p1, _) = zx::Channel::create().unwrap();

        let element = Element::from_directory_channel(
            p1,
            &name.to_string(),
            &url.to_string(),
            &collection.to_string(),
        );

        let (element_controller_proxy, element_controller_server) =
            create_proxy_and_stream::<fidl_fuchsia_session::ElementControllerMarker>()
                .expect("Failed to create ElementController proxy and server.");

        fasync::Task::local(handle_element_controller_request_stream(
            element_controller_server,
            element,
        ))
        .detach();

        let annotation = fidl_fuchsia_session::Annotation {
            key: "key".to_string(),
            value: Some(Box::new(fidl_fuchsia_session::Value::Text("value".to_string()))),
        };
        let annotations =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation]) };

        let annotation_2 = fidl_fuchsia_session::Annotation {
            key: "key".to_string(),
            value: Some(Box::new(fidl_fuchsia_session::Value::Text("value".to_string()))),
        };
        let annotations_2 =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation_2]) };

        let _ = element_controller_proxy.set_annotations(annotations).await.unwrap();

        let returned_annotations = element_controller_proxy.get_annotations().await;
        assert!(returned_annotations.is_ok());
        let unwrapped_annotations = returned_annotations.unwrap();
        assert!(unwrapped_annotations.is_ok());
        assert_eq!(unwrapped_annotations.unwrap(), annotations_2);

        let annotation_3 = fidl_fuchsia_session::Annotation {
            key: "key".to_string(),
            value: Some(Box::new(fidl_fuchsia_session::Value::Text("new_value".to_string()))),
        };
        let annotations_3 =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation_3]) };

        let annotation_4 = fidl_fuchsia_session::Annotation {
            key: "key".to_string(),
            value: Some(Box::new(fidl_fuchsia_session::Value::Text("new_value".to_string()))),
        };
        let annotations_4 =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation_4]) };

        let _ = element_controller_proxy.set_annotations(annotations_3).await.unwrap();

        let returned_annotations = element_controller_proxy.get_annotations().await;
        assert!(returned_annotations.is_ok());
        assert!(returned_annotations.is_ok());
        let unwrapped_annotations = returned_annotations.unwrap();
        assert!(unwrapped_annotations.is_ok());
        assert_eq!(unwrapped_annotations.unwrap(), annotations_4);
    }

    /// Tests that updating a Annotation to have a Value of None removes it from the custom_annotations
    /// vector.
    #[fasync::run_singlethreaded(test)]
    async fn remove_annotations_test() {
        let url = "component_url";
        let collection = "collection";
        let name = "name";
        let (p1, _) = zx::Channel::create().unwrap();

        let element = Element::from_directory_channel(
            p1,
            &name.to_string(),
            &url.to_string(),
            &collection.to_string(),
        );

        let (element_controller_proxy, element_controller_server) =
            create_proxy_and_stream::<fidl_fuchsia_session::ElementControllerMarker>()
                .expect("Failed to create ElementController proxy and server.");

        fasync::Task::local(handle_element_controller_request_stream(
            element_controller_server,
            element,
        ))
        .detach();

        let annotation = fidl_fuchsia_session::Annotation {
            key: "key".to_string(),
            value: Some(Box::new(fidl_fuchsia_session::Value::Text("value".to_string()))),
        };
        let annotations =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation]) };

        let _ = element_controller_proxy.set_annotations(annotations).await.unwrap();

        let annotation_2 = fidl_fuchsia_session::Annotation {
            key: "key".to_string(),
            value: Some(Box::new(fidl_fuchsia_session::Value::Text("value".to_string()))),
        };
        let annotations_2 =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation_2]) };

        // Check that there are Annotations.
        let returned_annotations = element_controller_proxy.get_annotations().await;
        assert!(returned_annotations.is_ok());
        let unwrapped_annotations = returned_annotations.unwrap();
        assert!(unwrapped_annotations.is_ok());
        assert_eq!(unwrapped_annotations.unwrap(), annotations_2);

        // Remove the Annotations.
        let annotation_3 = fidl_fuchsia_session::Annotation { key: "key".to_string(), value: None };
        let annotations_3 =
            fidl_fuchsia_session::Annotations { custom_annotations: Some(vec![annotation_3]) };
        let _ = element_controller_proxy.set_annotations(annotations_3).await.unwrap();

        let returned_annotations = element_controller_proxy.get_annotations().await;
        assert!(returned_annotations.is_ok());
        let unwrapped_annotations = returned_annotations.unwrap();
        assert!(unwrapped_annotations.is_ok());

        // Verify that there are no Annotations stored.
        assert_eq!(unwrapped_annotations.unwrap().custom_annotations.unwrap().len(), 0);
    }
}
