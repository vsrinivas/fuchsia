// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_element as element,
    fidl_fuchsia_session_scene::{
        ManagerMarker as SceneManagerMarker, ManagerProxy as SceneManagerProxy,
    },
    fidl_fuchsia_ui_app as ui_app, fidl_fuchsia_ui_views as ui_views, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs, server::ServiceObj},
    fuchsia_scenic::{self as scenic, flatland},
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::{channel::mpsc::UnboundedSender, StreamExt, TryStreamExt},
};

enum ExposedServices {
    GraphicalPresenter(element::GraphicalPresenterRequestStream),
}

enum MessageInternal {
    GraphicalPresenterPresentView {
        view_spec: element::ViewSpec,
        annotation_controller: Option<element::AnnotationControllerProxy>,
        view_controller_request_stream: Option<element::ViewControllerRequestStream>,
        responder: element::GraphicalPresenterPresentViewResponder,
    },
}

// The maximum number of concurrent services to serve.
const NUM_CONCURRENT_REQUESTS: usize = 5;

#[fuchsia::component(logging = true)]
async fn main() -> Result<(), Error> {
    let result = inner_main().await;
    if let Err(e) = result {
        fx_log_err!("Uncaught error in main(): {}", e);
        return Err(e);
    }
    Ok(())
}

// TODO(fxbug.dev/89425): Ideally we wouldn't need to have separate inner_main() and main()
// functions in order to catch and log top-level errors.  Instead, the #[fuchsia::component] macro
// could catch and log the error.
async fn inner_main() -> Result<(), Error> {
    let (internal_sender, mut internal_receiver) =
        futures::channel::mpsc::unbounded::<MessageInternal>();

    // We start listening for service requests, but don't yet start serving those requests until we
    // we receive confirmation that we are hooked up to the Scene Manager.
    let fs = expose_services()?;

    let scene_manager = connect_to_protocol::<SceneManagerMarker>()
        .expect("failed to connect to fuchsia.scene.Manager");
    let flatland = connect_to_protocol::<flatland::FlatlandMarker>()
        .expect("failed to connect to fuchsia.ui.composition.Flatland");
    let mut id_generator = flatland::IdGenerator::new();

    let root_transform_id = id_generator.next_transform_id();
    let _view_ref =
        set_scene_manager_root_view(&scene_manager, &flatland, root_transform_id.clone()).await?;

    // TODO(fxbug.dev/88656): do something like this to instantiate the library component that knows
    // how to generate a Flatland scene to lay views out on a tiled grid.  It will be used in the
    // event loop below.
    // let tiles_helper = tile_helper::TilesHelper::new();

    run_services(fs, internal_sender.clone());

    // TODO(fxbug.dev/88656): if we encapsulate all of the state in e.g. "struct TilesSession {...}"
    // then we could make this more testable by having e.g.
    //   let session = TilesSession::new(...);
    //   ...
    //   while let Some(message) = internal_receiver.next().await {
    //       session.handle_message(message);
    //   }
    //
    // (or, if we think it's cleaner, still do the matching in the loop, and in each match arm,
    // unwrap the args and use them to call a method on the session, e.g.
    //    responder.send(session.present_view(view_spec, ...));
    //
    // This is more testable because we can set up the session and either create MessageInternals
    // for it to handle, or call methods on it, depending on which option above is chosen.
    while let Some(message) = internal_receiver.next().await {
        match message {
            // The ElementManager has asked us (via GraphicalPresenter::PresentView()) to display
            // the view provided by a newly-launched element.
            MessageInternal::GraphicalPresenterPresentView {
                view_spec,
                annotation_controller,
                view_controller_request_stream,
                responder,
            } => {
                // TODO(fxbug.dev/88656): embed the element in the not-yet-existant tile view.
                let _ = view_spec;
                let _ = annotation_controller;
                let _ = view_controller_request_stream;

                // TODO(fxbug.dev/88656): instead of just sending "success", we need to actually
                // install the view into the Flatland scene graph, respond to messages on the
                // ViewControllerRequestStream, etc.
                if let Err(e) = responder.send(&mut Ok(())) {
                    fx_log_warn!(
                        "Failed to send response for GraphicalPresenter.PresentView(): {}",
                        e
                    );
                }
            }
        }
    }

    Ok(())
}

async fn set_scene_manager_root_view(
    scene_manager: &SceneManagerProxy,
    flatland: &flatland::FlatlandProxy,
    root_transform_id: flatland::TransformId,
) -> Result<ui_views::ViewRef, Error> {
    let (view_provider, mut view_provider_request_stream) =
        create_request_stream::<ui_app::ViewProviderMarker>()?;

    // Don't await the result yet, because the future will not resolve until we handle the
    // ViewProvider request below.
    let view_ref = scene_manager.set_root_view(view_provider);

    while let Some(request) = view_provider_request_stream
        .try_next()
        .await
        .context("Failed to obtain next ViewProvider request from stream")?
    {
        match request {
            ui_app::ViewProviderRequest::CreateView { .. } => {
                return Err(anyhow!("ViewProvider impl only handles CreateView2()"));
            }
            ui_app::ViewProviderRequest::CreateViewWithViewRef { .. } => {
                return Err(anyhow!("ViewProvider impl only handles CreateView2()"));
            }
            ui_app::ViewProviderRequest::CreateView2 { args, .. } => {
                if let Some(mut view_creation_token) = args.view_creation_token {
                    flatland.create_transform(&mut root_transform_id.clone())?;
                    flatland.set_root_transform(&mut root_transform_id.clone())?;

                    let mut view_identity =
                        ui_views::ViewIdentityOnCreation::from(scenic::ViewRefPair::new()?);

                    let (_parent_viewport_watcher, parent_viewport_watcher_request) =
                        create_proxy::<flatland::ParentViewportWatcherMarker>()?;

                    let view_bound_protocols = flatland::ViewBoundProtocols::EMPTY;

                    flatland.create_view2(
                        &mut view_creation_token,
                        &mut view_identity,
                        view_bound_protocols,
                        parent_viewport_watcher_request,
                    )?;

                    flatland.present(flatland::PresentArgs {
                        requested_presentation_time: Some(0),
                        ..flatland::PresentArgs::EMPTY
                    })?;
                } else {
                    return Err(anyhow!("CreateView2() missing view_creation_token field"));
                }

                // Now that we've handled the ViewProvider request, we can await the ViewRef.
                let view_ref = view_ref.await?;

                return Ok(view_ref);
            }
        }
    }
    Err(anyhow!("ViewProvider request stream closed before CreateView2() was called"))
}

fn expose_services() -> Result<ServiceFs<ServiceObj<'static, ExposedServices>>, Error> {
    let mut fs = ServiceFs::new();

    // Add services for component outgoing directory.
    fs.dir("svc").add_fidl_service(ExposedServices::GraphicalPresenter);
    fs.take_and_serve_directory_handle()?;

    Ok(fs)
}

fn run_services(
    fs: ServiceFs<ServiceObj<'static, ExposedServices>>,
    internal_sender: UnboundedSender<MessageInternal>,
) {
    fasync::Task::local(async move {
        fs.for_each_concurrent(NUM_CONCURRENT_REQUESTS, |service_request: ExposedServices| async {
            match service_request {
                ExposedServices::GraphicalPresenter(request_stream) => {
                    run_graphical_presenter_service(request_stream, internal_sender.clone());
                }
            }
        })
        .await;
    })
    .detach();
}

fn run_graphical_presenter_service(
    mut request_stream: element::GraphicalPresenterRequestStream,
    internal_sender: UnboundedSender<MessageInternal>,
) {
    fasync::Task::local(async move {
        while let Ok(Some(request)) = request_stream.try_next().await {
            match request {
                element::GraphicalPresenterRequest::PresentView {
                    view_spec,
                    annotation_controller,
                    view_controller_request,
                    responder,
                } => {
                    // "Unwrap" the optional element::AnnotationControllerProxy.
                    let annotation_controller = match annotation_controller {
                        Some(proxy) => match proxy.into_proxy() {
                            Ok(proxy) => Some(proxy),
                            Err(e) => {
                                fx_log_warn!("Failed to obtain AnnotationControllerProxy: {}", e);
                                None
                            }
                        },
                        None => None,
                    };
                    // "Unwrap" the optional element::ViewControllerRequestStream.
                    let view_controller_request_stream = match view_controller_request {
                        Some(request_stream) => match request_stream.into_stream() {
                            Ok(request_stream) => Some(request_stream),
                            Err(e) => {
                                fx_log_warn!("Failed to obtain ViewControllerRequestStream: {}", e);
                                None
                            }
                        },
                        None => None,
                    };
                    internal_sender
                        .unbounded_send(
                            MessageInternal::GraphicalPresenterPresentView {
                                view_spec,
                                annotation_controller,
                                view_controller_request_stream,
                                responder,
                            },
                            // TODO(fxbug.dev/88656): is this a safe expect()?  I think so, since
                            // we're using Task::local() instead of Task::spawn(), so we're on the
                            // same thread as main(), which will keep the receiver end alive until
                            // it exits, at which time the executor will not tick this task again.
                            // Assuming that we verify this understanding, what is the appropriate
                            // way to document this understanding?  Is it so idiomatic it needs no
                            // comment?  We're all Rust n00bs here, so maybe not?
                        )
                        .expect("Failed to send MessageInternal.");
                }
            }
        }
        // TODO(fxbug.dev/88656): if the result of try_next() is Err, we should probably log that instead of
        // silently swallowing it.
    })
    .detach();
}
