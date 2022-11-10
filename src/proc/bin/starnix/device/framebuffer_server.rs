// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file contains for creating and serving a `Flatland` view using a `Framebuffer`.
//!
//! A lot of the code in this file is temporary to enable developers to see the contents of a
//! `Framebuffer` in the workstation UI (e.g., using `ffx session add`).
//!
//! To display the `Framebuffer` as its view, a component must add the `framebuffer` feature to its
//! `.cml`.

use anyhow::anyhow;
use fidl::{endpoints::create_proxy, HandleBased};
use fidl_fuchsia_math as fmath;
use fidl_fuchsia_sysmem as fsysmem;
use fidl_fuchsia_ui_app as fuiapp;
use fidl_fuchsia_ui_composition as fuicomposition;
use fidl_fuchsia_ui_views as fuiviews;
use flatland_frame_scheduling_lib::{
    PresentationInfo, PresentedInfo, SchedulingLib, ThroughputScheduler,
};
use fuchsia_async as fasync;
use fuchsia_component::{client::connect_channel_to_protocol, server::ServiceFs};
use fuchsia_framebuffer::{sysmem::BufferCollectionAllocator, FrameUsage};
use fuchsia_scenic::{BufferCollectionTokenPair, ViewRefPair};
use fuchsia_zircon as zx;
use futures::{StreamExt, TryStreamExt};
use std::sync::{mpsc::channel, Arc};

use crate::logging::log_warn;
use crate::types::*;

/// The width of the framebuffer image.
pub const IMAGE_WIDTH: u32 = 720;

/// The height of the framebuffer image.
pub const IMAGE_HEIGHT: u32 = 1200;

/// The offset at which the framebuffer will be placed. Assume a display width of 1920.
pub const TRANSLATION_X: i32 = 1920 / 2 - IMAGE_WIDTH as i32 / 2;

/// The Flatland identifier for the framebuffer image.
const IMAGE_ID: fuicomposition::ContentId = fuicomposition::ContentId { value: 2 };

/// The Flatland identifier for the transform associated with the framebuffer.
const TRANSFORM_ID: fuicomposition::TransformId = fuicomposition::TransformId { value: 3 };

/// The protocols that are exposed by the framebuffer server.
enum ExposedProtocols {
    ViewProvider(fuiapp::ViewProviderRequestStream),
}

/// A `FramebufferServer` contains initialized proxies to Flatland, as well as a buffer collection
/// that is registered with Flatland.
pub struct FramebufferServer {
    /// The Flatland proxy associated with this server.
    flatland: fuicomposition::FlatlandSynchronousProxy,

    /// The buffer collection that is registered with Flatland.
    collection: fsysmem::BufferCollectionInfo2,
}

impl FramebufferServer {
    /// Returns a `FramebufferServer` that has created a scene and registered a buffer with
    /// Flatland.
    pub fn new() -> Result<Self, Errno> {
        let (server_end, client_end) = zx::Channel::create().map_err(|_| errno!(ENOENT))?;
        connect_channel_to_protocol::<fuicomposition::AllocatorMarker>(server_end)
            .map_err(|_| errno!(ENOENT))?;
        let allocator = fuicomposition::AllocatorSynchronousProxy::new(client_end);

        let (server_end, client_end) = zx::Channel::create().map_err(|_| errno!(ENOENT))?;
        connect_channel_to_protocol::<fuicomposition::FlatlandMarker>(server_end)
            .map_err(|_| errno!(ENOENT))?;
        let flatland = fuicomposition::FlatlandSynchronousProxy::new(client_end);

        let collection = init_scene(&flatland, &allocator).map_err(|_| errno!(EINVAL))?;

        Ok(Self { flatland, collection })
    }

    /// Returns a clone of the VMO that is shared with Flatland.
    pub fn get_vmo(&self) -> Result<zx::Vmo, Errno> {
        self.collection.buffers[0]
            .vmo
            .as_ref()
            .ok_or_else(|| errno!(EINVAL))?
            .duplicate_handle(zx::Rights::SAME_RIGHTS)
            .map_err(|_| errno!(EINVAL))
    }
}

/// Initializes the flatland scene, and returns the associated buffer collection.
///
/// SAFETY: This function `.expect`'s a lot, because it isn't meant to be used in the long time and
/// most of the failures would be unexpected and unrecoverable.
fn init_scene(
    flatland: &fuicomposition::FlatlandSynchronousProxy,
    allocator: &fuicomposition::AllocatorSynchronousProxy,
) -> Result<fsysmem::BufferCollectionInfo2, anyhow::Error> {
    let (collection_sender, collection_receiver) = channel();
    let (allocation_sender, allocation_receiver) = channel();
    // This thread is spawned to deal with the mix of asynchronous and synchronous proxies.
    // In particular, we want to keep Framebuffer creation synchronous, while still making use of
    // BufferCollectionAllocator (which exposes an async api).
    //
    // The spawned thread will execute the futures and send results back to this thread via a
    // channel.
    std::thread::spawn(move || -> Result<(), anyhow::Error> {
        let mut executor = fasync::LocalExecutor::new()?;

        let mut buffer_allocator = BufferCollectionAllocator::new(
            IMAGE_WIDTH,
            IMAGE_HEIGHT,
            fidl_fuchsia_sysmem::PixelFormatType::R8G8B8A8,
            FrameUsage::Cpu,
            1,
        )?;
        buffer_allocator.set_name(100, "Starnix ViewProvider")?;

        let sysmem_buffer_collection_token =
            executor.run_singlethreaded(buffer_allocator.duplicate_token())?;
        // Notify the async code that the sysmem buffer collection token is available.
        collection_sender.send(sysmem_buffer_collection_token).expect("Failed to send collection");

        let allocation = executor.run_singlethreaded(buffer_allocator.allocate_buffers(true))?;
        // Notify the async code that the buffer allocation completed.
        allocation_sender.send(allocation).expect("Failed to send allocation");

        Ok(())
    });

    // Wait for the async code to generate the buffer collection token.
    let sysmem_buffer_collection_token = collection_receiver
        .recv()
        .map_err(|_| anyhow!("Error receiving buffer collection token"))?;

    let mut buffer_tokens = BufferCollectionTokenPair::new();
    let args = fuicomposition::RegisterBufferCollectionArgs {
        export_token: Some(buffer_tokens.export_token),
        buffer_collection_token: Some(sysmem_buffer_collection_token),
        ..fuicomposition::RegisterBufferCollectionArgs::EMPTY
    };

    allocator
        .register_buffer_collection(args, zx::Time::INFINITE)
        .map_err(|_| anyhow!("FIDL error registering buffer collection"))?
        .map_err(|_| anyhow!("Error registering buffer collection"))?;

    // Now that the buffer collection is registered, wait for the buffer allocation to happen.
    let allocation =
        allocation_receiver.recv().map_err(|_| anyhow!("Error receiving buffer allocation"))?;

    let image_props = fuicomposition::ImageProperties {
        size: Some(fmath::SizeU { width: IMAGE_WIDTH, height: IMAGE_HEIGHT }),
        ..fuicomposition::ImageProperties::EMPTY
    };
    flatland
        .create_image(&mut IMAGE_ID.clone(), &mut buffer_tokens.import_token, 0, image_props)
        .map_err(|_| anyhow!("FIDL error creating image"))?;
    flatland
        .create_transform(&mut TRANSFORM_ID.clone())
        .map_err(|_| anyhow!("error creating transform"))?;
    flatland
        .set_root_transform(&mut TRANSFORM_ID.clone())
        .map_err(|_| anyhow!("error setting root transform"))?;
    flatland
        .set_content(&mut TRANSFORM_ID.clone(), &mut IMAGE_ID.clone())
        .map_err(|_| anyhow!("error setting content"))?;
    flatland
        .set_translation(&mut TRANSFORM_ID.clone(), &mut fmath::Vec_ { x: TRANSLATION_X, y: 0 })
        .map_err(|_| anyhow!("error setting translation"))?;

    Ok(allocation)
}

/// Spawns a thread to serve a `ViewProvider` in `outgoing_dir`.
///
/// SAFETY: This function `.expect`'s a lot, because it isn't meant to be used in the long time and
/// most of the failures would be unexpected and unrecoverable.
pub fn spawn_view_provider(
    server: Arc<FramebufferServer>,
    outgoing_dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
) {
    std::thread::spawn(|| {
        let mut executor = fasync::LocalExecutor::new().expect("Failed to create executor");
        executor.run_singlethreaded(async move {
            let mut service_fs = ServiceFs::new_local();
            service_fs.dir("svc").add_fidl_service(ExposedProtocols::ViewProvider);
            service_fs.serve_connection(outgoing_dir).expect("");

            while let Some(ExposedProtocols::ViewProvider(mut request_stream)) =
                service_fs.next().await
            {
                while let Ok(Some(event)) = request_stream.try_next().await {
                    match event {
                        fuiapp::ViewProviderRequest::CreateView2 { args, control_handle: _ } => {
                            let mut view_creation_token = args.view_creation_token.unwrap();
                            let mut view_identity = fuiviews::ViewIdentityOnCreation::from(
                                ViewRefPair::new().expect("Failed to create ViewRefPair"),
                            );
                            let view_bound_protocols = fuicomposition::ViewBoundProtocols {
                                ..fuicomposition::ViewBoundProtocols::EMPTY
                            };
                            // We don't actually care about the parent viewport at the moment, because we don't resize.
                            let (_parent_viewport_watcher, parent_viewport_watcher_request) =
                                create_proxy::<fuicomposition::ParentViewportWatcherMarker>()
                                    .expect("failed to create ParentViewportWatcherProxy");
                            server
                                .flatland
                                .create_view2(
                                    &mut view_creation_token,
                                    &mut view_identity,
                                    view_bound_protocols,
                                    parent_viewport_watcher_request,
                                )
                                .expect("FIDL error");

                            server
                                .flatland
                                .set_image_destination_size(
                                    &mut IMAGE_ID.clone(),
                                    &mut fmath::SizeU { width: IMAGE_WIDTH, height: IMAGE_HEIGHT },
                                )
                                .expect("fidl error");

                            // Now that the view has been created, start presenting.
                            start_presenting(server.clone());
                        }
                        r => {
                            log_warn!("Got unexpected view provider request: {:?}", r);
                        }
                    }
                }
            }
        });
    });
}

/// Starts a flatland presentation loop, using the flatland proxy in `server`.
fn start_presenting(server: Arc<FramebufferServer>) {
    fasync::Task::local(async move {
        let sched_lib = ThroughputScheduler::new();
        // Request an initial presentation.
        sched_lib.request_present();

        loop {
            let present_parameters = sched_lib.wait_to_update().await;
            sched_lib.request_present();
            server
                .flatland
                .present(fuicomposition::PresentArgs {
                    requested_presentation_time: Some(
                        present_parameters.requested_presentation_time.into_nanos(),
                    ),
                    acquire_fences: None,
                    release_fences: None,
                    unsquashable: Some(present_parameters.unsquashable),
                    ..fuicomposition::PresentArgs::EMPTY
                })
                .unwrap_or(());

            // Wait for events from flatland. If the event is `OnFramePresented` we notify the
            // scheduler and then wait for a `OnNextFrameBegin` before continuing.
            while match server.flatland.wait_for_event(zx::Time::INFINITE) {
                Ok(event) => match event {
                    fuicomposition::FlatlandEvent::OnNextFrameBegin { values } => {
                        let fuicomposition::OnNextFrameBeginValues {
                            additional_present_credits,
                            future_presentation_infos,
                            ..
                        } = values;
                        let infos = future_presentation_infos
                            .unwrap()
                            .iter()
                            .map(|x| PresentationInfo {
                                latch_point: zx::Time::from_nanos(x.latch_point.unwrap()),
                                presentation_time: zx::Time::from_nanos(
                                    x.presentation_time.unwrap(),
                                ),
                            })
                            .collect();
                        sched_lib.on_next_frame_begin(additional_present_credits.unwrap(), infos);
                        false
                    }
                    fuicomposition::FlatlandEvent::OnFramePresented { frame_presented_info } => {
                        let presented_infos = frame_presented_info
                            .presentation_infos
                            .iter()
                            .map(|info| PresentedInfo {
                                present_received_time: zx::Time::from_nanos(
                                    info.present_received_time.unwrap(),
                                ),
                                actual_latch_point: zx::Time::from_nanos(
                                    info.latched_time.unwrap(),
                                ),
                            })
                            .collect();

                        sched_lib.on_frame_presented(
                            zx::Time::from_nanos(frame_presented_info.actual_presentation_time),
                            presented_infos,
                        );
                        true
                    }
                    fuicomposition::FlatlandEvent::OnError { .. } => false,
                },
                Err(_) => false,
            } {}
        }
    })
    .detach();
}
