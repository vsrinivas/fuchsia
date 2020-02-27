// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    element_management::Element,
    fidl_fuchsia_session::{
        AnnotationError, ElementControllerRequest, ElementControllerRequestStream,
    },
    fuchsia_async as fasync,
    futures::TryStreamExt,
};

/// A trait which is used by the ElementRepository to respond to incoming events.
pub(crate) trait EventHandler {
    /// Called when a new element is added to the repository.
    fn add_element(&mut self, element: Element, stream: Option<ElementControllerRequestStream>);

    /// An optional method which will be called when the repository receives a shutdown event
    /// Implementers of this method can perform any extra cleanup if they need to.
    fn shutdown(&mut self) {}
}

pub(crate) struct ElementEventHandler {
    elements: Vec<ElementHolder>,
}

/// The ElementEventHandler is a concrete implementation of the EventHandler trait which
/// manages the lifecycle of elements.
impl ElementEventHandler {
    pub fn new() -> ElementEventHandler {
        ElementEventHandler { elements: vec![] }
    }
}

impl EventHandler for ElementEventHandler {
    fn add_element(&mut self, element: Element, stream: Option<ElementControllerRequestStream>) {
        self.elements.push(ElementHolder::new(element, stream));
    }

    // We do not implement the shutdown method because we do not have any cleanup to do at this time.
}

struct ElementHolder {
    _element: Option<Element>,
}

/// An ElementHolder helps to manage the lifetime of an Element.
///
/// If an element has an associated ElementController it will serve the ElementController protocol.
/// If not, it will just hold the elemen to keep it alive.
impl ElementHolder {
    fn new(element: Element, stream: Option<ElementControllerRequestStream>) -> ElementHolder {
        //TODO(47078): Watch the element and respond when it closes.
        if let Some(stream) = stream {
            ElementHolder::spawn_request_stream_server(element, stream);
            return ElementHolder::empty();
        }
        ElementHolder { _element: Some(element) }
    }

    fn spawn_request_stream_server(
        mut element: Element,
        mut stream: ElementControllerRequestStream,
    ) {
        //TODO(47078): need to shut down the request stream when the component the element represents dies.
        fasync::spawn_local(async move {
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
                        let _ = responder.send(
                            &mut element
                                .get_annotations()
                                .map_err(|_: anyhow::Error| AnnotationError::NotFound),
                        );
                    }
                }
            }
        });
    }

    // An empty ElementHolder is returned to signal that we are managing the lifetime of this
    // element but we are doing so via request stream loop.
    fn empty() -> ElementHolder {
        ElementHolder { _element: None }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::element_repository::testing_utils::{init_logger, make_mock_element},
        anyhow::Error,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_session::{Annotation, Annotations, ElementControllerMarker, Value},
    };
    #[test]
    fn add_element_adds_to_the_collection() {
        init_logger();
        let (element, _channel) = make_mock_element();
        let mut handler = ElementEventHandler::new();
        handler.add_element(element, None);
        assert_eq!(handler.elements.len(), 1);
    }

    #[test]
    fn element_holder_without_stream_new() {
        init_logger();
        let (element, _channel) = make_mock_element();
        let holder = ElementHolder::new(element, None);
        assert!(holder._element.is_some());
    }

    #[fasync::run_singlethreaded(test)]
    async fn element_holder_with_stream_manages_lifetime_via_stream() -> Result<(), Error> {
        init_logger();
        let (element, _channel) = make_mock_element();
        let (_proxy, stream) = create_proxy_and_stream::<ElementControllerMarker>()?;
        let holder = ElementHolder::new(element, Some(stream));
        assert!(holder._element.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn element_holder_listens_to_event_stream() -> Result<(), Error> {
        init_logger();
        let (element, _channel) = make_mock_element();
        let (proxy, stream) = create_proxy_and_stream::<ElementControllerMarker>()?;
        let _holder = ElementHolder::new(element, Some(stream));

        let annotation = Annotation {
            key: "foo".to_string(),
            value: Some(Box::new(Value::Text("bar".to_string()))),
        };
        let _ =
            proxy.set_annotations(Annotations { custom_annotations: Some(vec![annotation]) }).await;
        let result = proxy.get_annotations().await?;

        let key = &result.unwrap().custom_annotations.unwrap()[0].key;
        assert_eq!(key, "foo");
        Ok(())
    }
}
