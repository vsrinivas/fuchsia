// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    element_management::Element,
    fidl::encoding::Decodable,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_session::{
        AnnotationError, Annotations, ElementControllerRequest, ElementControllerRequestStream,
        GraphicalPresenterProxy, ViewControllerMarker, ViewControllerProxy, ViewSpec,
    },
    fidl_fuchsia_ui_app::ViewProviderMarker,
    fuchsia_async as fasync, fuchsia_scenic as scenic,
    fuchsia_zircon as zx,
    fuchsia_zircon::AsHandleRef,
    futures::{select, stream::StreamExt, FutureExt, TryStreamExt},
    std::{cell::RefCell, rc::Rc},
};

/// A trait which is used by the ElementRepository to respond to incoming events.
pub trait EventHandler {
    /// Called when a new element is added to the repository.
    fn add_element(&mut self, element: Element, stream: Option<ElementControllerRequestStream>);

    /// An optional method which will be called when the repository receives a shutdown event
    /// Implementers of this method can perform any extra cleanup if they need to.
    fn shutdown(&mut self) {}
}

pub struct ElementEventHandler {
    proxy: GraphicalPresenterProxy,
}

/// The ElementEventHandler is a concrete implementation of the EventHandler trait which
/// manages the lifecycle of elements.
impl ElementEventHandler {
    /// Creates a new instance of the ElementEventHandler.
    pub fn new(proxy: GraphicalPresenterProxy) -> ElementEventHandler {
        ElementEventHandler { proxy }
    }

    /// Attempts to connect to the element's view provider and if it does
    /// expose the view provider will tell the proxy to present the view.
    fn present_view_for_element(
        &self,
        element: &mut Element,
    ) -> Result<ViewControllerProxy, Error> {
        let view_provider = element.connect_to_service::<ViewProviderMarker>()?;
        let token_pair = scenic::ViewTokenPair::new()?;
        let scenic::ViewRefPair { mut control_ref, mut view_ref } = scenic::ViewRefPair::new()?;
        let view_ref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;

        // note: this call will never fail since connecting to a service is
        // always successful and create_view doesn't have a return value.
        // If there is no view provider, the view_holder_token will be invalidated
        // and the presenter can choose to close the view controller if it
        // only wants to allow graphical views.
        view_provider.create_view_with_view_ref(
            token_pair.view_token.value,
            &mut control_ref,
            &mut view_ref,
        )?;

        let annotations = element.get_annotations()?;

        let view_spec = ViewSpec {
            view_holder_token: Some(token_pair.view_holder_token),
            view_ref: Some(view_ref_dup),
            annotations: Some(annotations),
            ..ViewSpec::empty()
        };

        let (view_controller_proxy, server_end) = create_proxy::<ViewControllerMarker>()?;
        self.proxy.present_view(view_spec, Some(server_end))?;

        Ok(view_controller_proxy)
    }
}

impl EventHandler for ElementEventHandler {
    fn add_element(
        &mut self,
        mut element: Element,
        stream: Option<ElementControllerRequestStream>,
    ) {
        // Attempt to present the view. For now, if this fails we log the error and continue, In the
        // future we will want to communicate the failure back to the proposer.
        let view_controller_proxy = self.present_view_for_element(&mut element).ok();

        // Hold the element in the spawn_local here. when the call closes all
        // of the proxies will be closed.
        fasync::Task::local(async move {
            run_until_closed(element, stream, view_controller_proxy).await;
        })
        .detach();
    }
}

/// Runs the Element until it receives a signal to shutdown.
///
/// The Element can receive a signal to shut down from any of the
/// following:
///   - Element. The component represented by the element can close on its own.
///   - ElementControllerRequestStream. The element controller can signal that the element should close.
///   - ViewControllerProxy. The view controller can signal that the element can close.
///
/// The Element will shutdown when any of these signals are received.
///
/// The Element will also listen for any incoming events from the element controller and
/// forward them to the view controller.
async fn run_until_closed(
    element: Element,
    stream: Option<ElementControllerRequestStream>,
    view_controller_proxy: Option<ViewControllerProxy>,
) {
    let element = Rc::new(RefCell::new(element));

    select!(
        _ = await_element_close(element.clone()).fuse() => {
            // signals that a element has died without being told to close.
            // We could tell the view to dismiss here but we need to signal
            // that there was a crash. The current contract is that if the
            // view controller binding closes without a dismiss then the
            // presenter should treat this as a crash and respond accordingly.
            if let Some(proxy) = view_controller_proxy {
                // We want to allow the presenter the ability to dismiss
                // the view so we tell it to dismiss and then wait for
                // the view controller stream to close.
                let _ = proxy.dismiss();
                //TODO(47925) introdue a timeout here
                wait_for_view_controller_close(proxy).await;
            }
        },
        _ = wait_for_optional_view_controller_close(view_controller_proxy.clone()).fuse() =>  {
            // signals that the presenter would like to close the element.
            // We do not need to do anything here but exit which will cause
            // the element to be dropped and will kill the component.
        },
        _ = spawn_element_controller_stream(element.clone(), stream, view_controller_proxy.clone()).fuse() => {
            // the proposer has decided they want to shut down the element.
            if let Some(proxy) = view_controller_proxy {
                // We want to allow the presenter the ability to dismiss
                // the view so we tell it to dismiss and then wait for
                // the view controller stream to close.
                let _ = proxy.dismiss();
                //TODO(47925) introdue a timeout here
                wait_for_view_controller_close(proxy).await;
            }
        }
    );
}

/// Waits for the element to signal that it closed
async fn await_element_close(element: Rc<RefCell<Element>>) {
    let element = element.borrow();
    let channel = element.directory_channel();
    let _ =
        fasync::OnSignals::new(&channel.as_handle_ref(), zx::Signals::CHANNEL_PEER_CLOSED).await;
}

/// Waits for the view controller to signal that it wants to close.
///
/// if the ViewControllerProxy is None then this future will never resolve.
async fn wait_for_optional_view_controller_close(proxy: Option<ViewControllerProxy>) {
    if let Some(proxy) = proxy {
        wait_for_view_controller_close(proxy).await;
    } else {
        // If the view controller is None then we never exit and rely
        // on the other futures to signal the end of the element.
        futures::future::pending::<()>().await;
    }
}

/// Waits for this view controller to close.
async fn wait_for_view_controller_close(proxy: ViewControllerProxy) {
    let stream = proxy.take_event_stream();
    let _ = stream.collect::<Vec<_>>().await;
}

/// watches the element controller request stream and responds to requests.
///
/// if the ElementControllerRequestStream is None then this future will never resolve.
async fn spawn_element_controller_stream(
    element: Rc<RefCell<Element>>,
    stream: Option<ElementControllerRequestStream>,
    view_controller_proxy: Option<ViewControllerProxy>,
) {
    if let Some(mut stream) = stream {
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                ElementControllerRequest::SetAnnotations { annotations, responder } => {
                    let mut element = element.borrow_mut();
                    let _ = responder.send(
                        &mut element
                            .set_annotations(annotations)
                            .map_err(|_: anyhow::Error| AnnotationError::Rejected),
                    );

                    if let Some(proxy) = &view_controller_proxy {
                        // Annotations cannot be cloned so we get them from
                        // the element which creates a new copy. This must
                        // be done after the call to set_annotations on
                        // element.
                        let annotations = element.get_annotations().unwrap_or_else(|e: Error| {
                            eprintln!("failed to get annotations from element: {:?}", e);
                            Annotations::new_empty()
                        });
                        let _ =
                            proxy.annotate(annotations).await.unwrap_or_else(|e: fidl::Error| {
                                eprintln!("failed to get annotations from element: {:?}", e)
                            });
                    }
                }
                ElementControllerRequest::GetAnnotations { responder } => {
                    let mut element = element.borrow_mut();
                    let _ = responder.send(
                        &mut element
                            .get_annotations()
                            .map_err(|_: anyhow::Error| AnnotationError::NotFound),
                    );
                }
            }
        }
    } else {
        // If the element controller is None then we never exit and rely
        // on the other futures to signal the end of the element.
        futures::future::pending::<()>().await;
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::element_repository::testing_utils::{init_logger, make_mock_element},
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_session::{
            Annotation, ElementControllerMarker, GraphicalPresenterMarker, Value,
            ViewControllerMarker, ViewControllerRequest,
        },
        fuchsia_async::{DurationExt, Timer},
        fuchsia_zircon::DurationNum,
        futures::{future::Either, task::Poll},
    };

    macro_rules! expect_element_wait_fut_completion {
        ($element_wait_fut:expr) => {
            let timeout = Timer::new(500_i64.millis().after_now());
            let either = futures::future::select(timeout, $element_wait_fut);
            let resolved = either.await;
            match resolved {
                Either::Left(_) => panic!("failed to end element wait"),
                Either::Right(_) => (),
            }
        };
    }

    #[test]
    fn spawn_element_controller_stream_never_resolves_if_none_stream() {
        init_logger();
        let mut executor = fasync::Executor::new().unwrap();

        let (element, _channel) = make_mock_element();
        let mut fut =
            Box::pin(spawn_element_controller_stream(Rc::new(RefCell::new(element)), None, None));

        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut fut));
    }

    #[test]
    fn wait_for_optional_view_controller_close_never_resolves_if_none_proxy() {
        init_logger();
        let mut executor = fasync::Executor::new().unwrap();

        let mut fut = Box::pin(wait_for_optional_view_controller_close(None));

        assert_eq!(Poll::Pending, executor.run_until_stalled(&mut fut));
    }

    #[fasync::run_singlethreaded(test)]
    async fn add_element_keeps_the_element_alive() {
        init_logger();
        let (element, channel) = make_mock_element();
        let (proxy, _stream) =
            create_proxy_and_stream::<GraphicalPresenterMarker>().expect("failed to create proxy");

        let mut handler = ElementEventHandler::new(proxy);
        handler.add_element(element, None);

        let timeout = Timer::new(500_i64.millis().after_now());
        let handle_ref = channel.as_handle_ref();
        let element_close_fut =
            fasync::OnSignals::new(&handle_ref, zx::Signals::CHANNEL_PEER_CLOSED);

        let either = futures::future::select(timeout, element_close_fut);
        let resolved = either.await;
        match resolved {
            Either::Left(_) => (),
            Either::Right(_) => panic!("channel closed before timeout"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn killing_element_ends_wait() {
        init_logger();
        let (element, channel) = make_mock_element();

        let element_wait_fut = Box::pin(run_until_closed(element, None, None));

        // signal that the element should close
        drop(channel);

        expect_element_wait_fut_completion!(element_wait_fut);
    }

    #[fasync::run_singlethreaded(test)]
    async fn dropping_element_controller_ends_wait() {
        init_logger();
        let (element, _channel) = make_mock_element();
        let (element_controller, element_stream) =
            create_proxy_and_stream::<ElementControllerMarker>().expect("failed to create proxy");
        let element_wait_fut = Box::pin(run_until_closed(element, Some(element_stream), None));

        drop(element_controller);

        expect_element_wait_fut_completion!(element_wait_fut);
    }

    #[fasync::run_singlethreaded(test)]
    async fn dropping_element_controller_ends_wait_after_dismissing_proxy() {
        init_logger();
        let (element, _channel) = make_mock_element();
        let (vc_proxy, vc_server_end) =
            create_proxy::<ViewControllerMarker>().expect("failed to create proxy");

        let (element_controller, element_stream) =
            create_proxy_and_stream::<ElementControllerMarker>().expect("failed to create proxy");

        let element_wait_fut =
            Box::pin(run_until_closed(element, Some(element_stream), Some(vc_proxy)));

        drop(element_controller);

        // Our API contract is that when the element controller closes we
        // tell the view controller to dismiss. The view controller then can
        // decide how it wants to dismiss the view. When it is done presenting
        // the view it closes the channel to indicate it is done presenting.
        fasync::Task::local(async move {
            let mut vc_stream = vc_server_end.into_stream().unwrap();
            while let Ok(Some(request)) = vc_stream.try_next().await {
                match request {
                    ViewControllerRequest::Dismiss { control_handle } => {
                        control_handle.shutdown();
                    }
                    _ => (),
                }
            }
        })
        .detach();

        expect_element_wait_fut_completion!(element_wait_fut);
    }

    #[fasync::run_singlethreaded(test)]
    async fn dropping_view_controller_ends_wait() {
        init_logger();
        let (element, _channel) = make_mock_element();
        let (view_controller, view_controller_stream) =
            create_proxy_and_stream::<ViewControllerMarker>().expect("failed to create proxy");
        let element_wait_fut = Box::pin(run_until_closed(element, None, Some(view_controller)));

        drop(view_controller_stream);

        expect_element_wait_fut_completion!(element_wait_fut);
    }

    #[fasync::run_singlethreaded(test)]
    async fn spawn_element_controller_stream_set_annotations_updates_element() {
        init_logger();
        let (element, _channel) = make_mock_element();
        let (element_controller, element_stream) =
            create_proxy_and_stream::<ElementControllerMarker>().expect("failed to create proxy");

        let element = Rc::new(RefCell::new(element));

        let element_clone = element.clone();
        fasync::Task::local(async move {
            spawn_element_controller_stream(element_clone, Some(element_stream), None).await;
        })
        .detach();

        let new_annotations = Annotations {
            custom_annotations: Some(vec![Annotation {
                key: "foo".to_string(),
                value: Some(Box::new(Value::Text("bar".to_string()))),
            }]),
        };
        let _ = element_controller.set_annotations(new_annotations).await;

        let mut element = element.borrow_mut();
        let annotations = element.get_annotations().unwrap();
        let custom_annotations = annotations.custom_annotations.unwrap();

        assert_eq!(custom_annotations.len(), 1);
        assert_eq!(custom_annotations[0].key, "foo");
    }

    #[fasync::run_singlethreaded(test)]
    async fn spawn_element_controller_stream_set_annotations_updates_view_controller_proxy() {
        init_logger();
        let (element, _channel) = make_mock_element();
        let (element_controller, element_stream) =
            create_proxy_and_stream::<ElementControllerMarker>().expect("failed to create proxy");
        let (vc_proxy, mut view_controller_stream) =
            create_proxy_and_stream::<ViewControllerMarker>().expect("failed to create proxy");

        let element = Rc::new(RefCell::new(element));

        let element_clone = element.clone();
        fasync::Task::local(async move {
            spawn_element_controller_stream(element_clone, Some(element_stream), Some(vc_proxy))
                .await;
        })
        .detach();

        fasync::Task::local(async move {
            let new_annotations = Annotations {
                custom_annotations: Some(vec![Annotation {
                    key: "foo".to_string(),
                    value: Some(Box::new(Value::Text("bar".to_string()))),
                }]),
            };
            let _ = element_controller.set_annotations(new_annotations).await;
        })
        .detach();

        let mut got_annotation = false;
        if let Ok(Some(request)) = view_controller_stream.try_next().await {
            match request {
                ViewControllerRequest::Annotate { annotations, responder } => {
                    let custom_annotations = annotations.custom_annotations.unwrap();
                    got_annotation = custom_annotations[0].key == "foo";

                    let _ = responder.send();
                }
                _ => (),
            }
        }
        assert!(got_annotation);
    }

    #[fasync::run_singlethreaded(test)]
    async fn spawn_element_controller_stream_can_get_annotations() -> Result<(), Error> {
        init_logger();

        let (element, _channel) = make_mock_element();
        let (element_controller, element_stream) =
            create_proxy_and_stream::<ElementControllerMarker>().expect("failed to create proxy");

        let element = Rc::new(RefCell::new(element));

        let new_annotations = Annotations {
            custom_annotations: Some(vec![Annotation {
                key: "foo".to_string(),
                value: Some(Box::new(Value::Text("bar".to_string()))),
            }]),
        };

        {
            let mut element = element.borrow_mut();
            element.set_annotations(new_annotations).expect("failed to set annotationss");
        }

        let element_clone = element.clone();
        fasync::Task::local(async move {
            spawn_element_controller_stream(element_clone, Some(element_stream), None).await;
        })
        .detach();

        let mut got_annotation = false;
        if let Ok(Ok(annotations)) = element_controller.get_annotations().await {
            let custom_annotations = annotations.custom_annotations.unwrap();
            got_annotation = custom_annotations[0].key == "foo";
        }
        assert!(got_annotation);
        Ok(())
    }
}
