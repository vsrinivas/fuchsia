// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use async_utils::hanging_get::client::HangingGetStream;
use fidl::endpoints::{create_proxy, ClientEnd};
use fidl_fuchsia_developer_tiles::{
    ControllerAddTileFromUrlResponder, ControllerAddTileFromViewProviderResponder,
    ControllerControlHandle, ControllerListTilesResponder, ControllerRequest,
    ControllerRequestStream,
};
use fidl_fuchsia_math as fmath;
use fidl_fuchsia_sys::{LauncherMarker, LauncherProxy};
use fidl_fuchsia_ui_app as ui_app;
use fidl_fuchsia_ui_gfx::Vec3;
use fuchsia_async as fasync;
use fuchsia_component as component;
use fuchsia_component::client::{connect_to_protocol, launch, App};
use fuchsia_scenic::{flatland, ViewRefPair};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    future,
    prelude::*,
};
use log::*;
use std::{collections::BTreeMap, convert::TryInto};

const ROOT_TRANSFORM_ID: flatland::TransformId = flatland::TransformId { value: u64::MAX };

struct Tile {
    url: String,
    focusable: bool,
    #[allow(dead_code)] // Keeps app alive until dropped.
    app: App,
}

struct Service {
    next_id: u32,
    tiles: BTreeMap<u32, Tile>,
    session: flatland::FlatlandProxy,
    launcher: LauncherProxy,
    num_presents_allowed: u32,
    pending_present: bool,
    logical_width: u32,
    logical_height: u32,
}

enum MessageInternal {
    ControllerRequest(ControllerRequest),
    FlatlandEvent(flatland::FlatlandEvent),
    ParentViewportWatcherGetLayout(flatland::LayoutInfo),
}

// Represents a grid of uniformly-sized rectangular cells.
struct GridSpec {
    column_count: u32,
    row_count: u32,
    column_width: u32,
    row_height: u32,
}

impl GridSpec {
    fn new(cell_count: u32, total_width: u32, total_height: u32) -> GridSpec {
        let mut column_count: u32 = 1;
        let mut row_count: u32 = 1;

        // Compute the number of rows and columns in the layout grid.  The resulting grid will
        // either be square, or have one less row than columns.
        loop {
            if column_count * row_count >= cell_count {
                break;
            };
            column_count += 1;
            if column_count * row_count >= cell_count {
                break;
            };
            row_count += 1;
        }

        let column_width = total_width / column_count;
        let row_height = total_height / row_count;

        GridSpec { column_count, row_count, column_width, row_height }
    }
}

// Service encapsulates all necessary state and provides methods that are called in response to FIDL
// messages from clients.
//
// Service takes an "optimistic" approach to error handling: it generally assumes that errors won't
// happen, and panics if they do.  There are some exceptions to this rule.  For example, if bad data
// is received from a FIDL client, this should not trigger a panic, just a log message.
//
// TODO(fxbug.dev/80814): currently we don't detect when a tile app dies.  Therefore, a dead app
// will still appear in the list, and will take up space in the layout.  It would be better to
// proactively remove dead tiles from the list.
impl Service {
    fn new(
        display: &flatland::FlatlandDisplayProxy,
        session: flatland::FlatlandProxy,
        launcher: LauncherProxy,
        internal_sender: UnboundedSender<MessageInternal>,
    ) -> Service {
        let mut link_tokens =
            flatland::LinkTokenPair::new().expect("failed to create LinkTokenPair");
        let (_, child_view_watcher_request) = create_proxy::<flatland::ChildViewWatcherMarker>()
            .expect("failed to create ChildViewWatcher endpoints");
        display
            .set_content(&mut link_tokens.viewport_creation_token, child_view_watcher_request)
            .expect("fidl error");

        let (parent_viewport_watcher_proxy, parent_viewport_watcher_request) =
            create_proxy::<flatland::ParentViewportWatcherMarker>()
                .expect("failed to create ParentViewportWatcher endpoints");
        session
            .create_view(&mut link_tokens.view_creation_token, parent_viewport_watcher_request)
            .expect("fidl error");

        fasync::Task::spawn(async move {
            let mut layout_info_stream = HangingGetStream::new(Box::new(move || {
                Some(parent_viewport_watcher_proxy.get_layout())
            }));

            while let Some(result) = layout_info_stream.next().await {
                match result {
                    Ok(layout_info) => {
                        internal_sender
                            .unbounded_send(MessageInternal::ParentViewportWatcherGetLayout(
                                layout_info,
                            ))
                            .expect("failed to send MessageInternal.");
                    }
                    Err(fidl_error) => {
                        warn!("graph link GetLayout() error: {:?}", fidl_error);
                        return; // from spawned task closure
                    }
                }
            }
        })
        .detach();

        session.create_transform(&mut ROOT_TRANSFORM_ID.clone()).expect("fidl error");
        session.set_root_transform(&mut ROOT_TRANSFORM_ID.clone()).expect("fidl error");

        Service {
            next_id: 1,
            tiles: BTreeMap::new(),
            session,
            launcher,
            num_presents_allowed: 1,
            pending_present: false,
            logical_width: 1280,
            logical_height: 1024,
        }
    }

    fn add_tile_from_url(
        &mut self,
        url: String,
        allow_focus: bool,
        args: Option<Vec<String>>,
        responder: ControllerAddTileFromUrlResponder,
    ) -> Result<(), Error> {
        let id = self.next_id;

        let app = launch(&self.launcher, url.clone(), args)?;
        let view_provider =
            app.connect_to_protocol::<ui_app::TemporaryFlatlandViewProviderMarker>()?;
        let mut link_tokens = flatland::LinkTokenPair::new()?;
        let mut view_ref_pair = ViewRefPair::new()?;
        view_provider
            .create_view(
                &mut link_tokens.view_creation_token,
                &mut view_ref_pair.control_ref,
                &mut view_ref_pair.view_ref,
            )
            .expect("fidl error");

        let mut transform_id = flatland::TransformId { value: id.into() };
        let mut link_id = flatland::ContentId { value: id.into() };

        // Compute initial size of tile.
        let tile_count: u32 = self.tiles.len().try_into().unwrap();
        let GridSpec { column_width, row_height, .. } =
            GridSpec::new(tile_count + 1, self.logical_width, self.logical_height);
        let link_properties = flatland::ViewportProperties {
            logical_size: Some(fmath::SizeU { width: column_width, height: row_height }),
            ..flatland::ViewportProperties::EMPTY
        };

        let (_, child_view_watcher_request) = create_proxy::<flatland::ChildViewWatcherMarker>()
            .expect("failed to create ChildViewWatcher endpoints");

        self.session.create_transform(&mut transform_id).expect("fidl error");
        self.session
            .create_viewport(
                &mut link_id,
                &mut link_tokens.viewport_creation_token,
                link_properties,
                child_view_watcher_request,
            )
            .expect("fidl error");
        self.session.set_content(&mut transform_id, &mut link_id).expect("fidl error");

        self.session
            .add_child(&mut ROOT_TRANSFORM_ID.clone(), &mut transform_id)
            .expect("fidl error");

        self.next_id = self.next_id + 1;
        self.tiles.insert(id, Tile { url: url, focusable: allow_focus, app: app });
        responder.send(id).context("AddTileFromUrl: failed to send ID to client")?;

        self.relayout();

        Ok(())
    }

    fn add_tile_from_view_provider(
        &mut self,
        _url: String,
        _provider: ClientEnd<ui_app::ViewProviderMarker>,
        _responder: ControllerAddTileFromViewProviderResponder,
    ) {
        error!("AddTileFromViewProvider is not implemented (and probably will not be).");
    }

    fn remove_tile(&mut self, key: u32, _control_handle: ControllerControlHandle) {
        if let Some(_tile) = self.tiles.remove_entry(&key) {
            let mut transform_id = flatland::TransformId { value: key.into() };
            let mut link_id = flatland::ContentId { value: key.into() };

            self.session
                .remove_child(&mut ROOT_TRANSFORM_ID.clone(), &mut transform_id)
                .expect("fidl error");
            self.session.release_transform(&mut transform_id).expect("fidl error");

            // When removing a tile, we don't intend to reparent it, so we drop the returned future.
            let _ = self.session.release_viewport(&mut link_id);

            self.relayout();
        } else {
            warn!("RemoveTile: Tried to remove non-existent tile with key {:?}", key)
        }
    }

    fn list_tiles(&mut self, responder: ControllerListTilesResponder) {
        let iter = self.tiles.iter();
        let keys = iter.clone().map(|(k, _)| *k);
        let mut urls = iter.clone().map(|(_, tile)| tile.url.as_str());
        let tile_count: u32 = self.tiles.len().try_into().unwrap();

        let spec = GridSpec::new(tile_count, self.logical_width, self.logical_height);
        let mut sizes: Vec<_> = iter
            .clone()
            .map(|_| Vec3 { x: spec.column_width as f32, y: spec.row_height as f32, z: 1.0 })
            .collect();
        let mut focusabilities = iter.clone().map(|(_, tile)| tile.focusable);
        if let Err(e) = responder.send(
            &keys.collect::<Vec<u32>>(),
            &mut urls,
            &mut sizes.iter_mut(),
            &mut focusabilities,
        ) {
            warn!("ListTiles: fidl response error: {:?}", e);
        }
    }

    fn quit(&mut self, _control_handle: ControllerControlHandle) {
        info!("recieved Quit message");
        std::process::exit(0);
    }

    fn relayout(&mut self) {
        let tile_count: u32 = self.tiles.len().try_into().unwrap();

        let GridSpec { column_count, row_count, column_width, row_height } =
            GridSpec::new(tile_count, self.logical_width, self.logical_height);
        assert!(tile_count <= column_count * row_count);

        for (i, (id, _tile)) in self.tiles.iter().enumerate() {
            let mut transform_id = flatland::TransformId { value: id.clone().into() };
            let i: u32 = i.try_into().unwrap();
            let y: i32 = ((i / column_count) * row_height).try_into().unwrap();
            let x: i32 = ((i % column_count) * column_width).try_into().unwrap();
            self.session
                .set_translation(&mut transform_id, &mut fmath::Vec_ { x, y })
                .expect("fidl error");

            let mut link_id = flatland::ContentId { value: id.clone().into() };
            let link_properties = flatland::ViewportProperties {
                logical_size: Some(fmath::SizeU { width: column_width, height: row_height }),
                ..flatland::ViewportProperties::EMPTY
            };
            self.session
                .set_viewport_properties(&mut link_id, link_properties)
                .expect("fidl error");
        }

        self.pending_present = true;
        self.maybe_present();
    }

    fn maybe_present(&mut self) {
        if self.num_presents_allowed > 0 && self.pending_present {
            self.pending_present = false;
            self.num_presents_allowed -= 1;
            self.session
                .present(flatland::PresentArgs {
                    requested_presentation_time: Some(0),
                    acquire_fences: None,
                    release_fences: None,
                    unsquashable: Some(true),
                    ..flatland::PresentArgs::EMPTY
                })
                .expect("fidl error");
        }
    }
}

fn setup_fidl_services(sender: UnboundedSender<MessageInternal>) {
    let tiles_controller_cb = move |stream: ControllerRequestStream| {
        let sender = sender.clone();
        fasync::Task::local(
            stream
                .try_for_each(move |req| {
                    sender
                        .unbounded_send(MessageInternal::ControllerRequest(req))
                        .expect("failed to send MessageInternal.");
                    future::ok(())
                })
                .unwrap_or_else(|e| eprintln!("error running Tiles Controller server: {:?}", e)),
        )
        .detach()
    };

    let mut fs = component::server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(tiles_controller_cb);

    fs.take_and_serve_directory_handle().expect("failed to serve directory handle");
    fasync::Task::local(fs.collect()).detach();
}

fn setup_handle_flatland_events(
    event_stream: flatland::FlatlandEventStream,
    sender: UnboundedSender<MessageInternal>,
) {
    fasync::Task::local(
        event_stream
            .try_for_each(move |event| {
                sender
                    .unbounded_send(MessageInternal::FlatlandEvent(event))
                    .expect("failed to send MessageInternal.");
                future::ok(())
            })
            .unwrap_or_else(|e| eprintln!("error listening for Flatland Events: {:?}", e)),
    )
    .detach();
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["tiles-flatland"]).expect("failed to initialize logger");

    let (internal_sender, mut internal_receiver) = unbounded::<MessageInternal>();

    let flatland_display = connect_to_protocol::<flatland::FlatlandDisplayMarker>()
        .expect("error connecting to Flatland display");
    let flatland_session = connect_to_protocol::<flatland::FlatlandMarker>()
        .expect("error connecting to Flatland session");
    info!("Established connections to Flatland display and session");

    setup_fidl_services(internal_sender.clone());
    setup_handle_flatland_events(flatland_session.take_event_stream(), internal_sender.clone());

    let launcher =
        connect_to_protocol::<LauncherMarker>().expect("error connecting to Launcher service");

    let mut service =
        Service::new(&flatland_display, flatland_session, launcher, internal_sender.clone());

    while let Some(message) = internal_receiver.next().await {
        match message {
            MessageInternal::ControllerRequest(request) => match request {
                ControllerRequest::AddTileFromUrl { url, allow_focus, args, responder } => {
                    if let Err(e) = service.add_tile_from_url(url, allow_focus, args, responder) {
                        info!("error in add_tile_from_url(): {:?}", e);
                    }
                }
                ControllerRequest::AddTileFromViewProvider { url, provider, responder } => {
                    service.add_tile_from_view_provider(url, provider, responder);
                }
                ControllerRequest::RemoveTile { key, control_handle } => {
                    service.remove_tile(key, control_handle);
                }
                ControllerRequest::ListTiles { responder } => {
                    service.list_tiles(responder);
                }
                ControllerRequest::Quit { control_handle } => {
                    service.quit(control_handle);
                }
            },
            MessageInternal::FlatlandEvent(event) => match event {
                flatland::FlatlandEvent::OnNextFrameBegin { values } => {
                    if let Some(additional_present_credits) = values.additional_present_credits {
                        service.num_presents_allowed += additional_present_credits;
                        service.maybe_present();
                    } else {
                        // All fields used above are guaranteed to be present in the table.
                        unreachable!()
                    }
                }
                flatland::FlatlandEvent::OnFramePresented { .. } => {}
                flatland::FlatlandEvent::OnError { error } => {
                    error!("OnPresentProcessed({:?})", error);
                }
            },
            MessageInternal::ParentViewportWatcherGetLayout(layout_info) => {
                if let Some(logical_size) = layout_info.logical_size {
                    service.logical_width = logical_size.width;
                    service.logical_height = logical_size.height;
                    service.relayout();
                }
            }
        }
    }

    info!("Exiting tiles-flatland, goodbye.");

    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn test_grid_spec() {
        use crate::GridSpec;

        // 9 cells fit on a 3x3 grid.  Given a total width of 33, and a total height of 66, the
        // width/height of each grid cell is 11/22.
        let spec = GridSpec::new(9, 33, 66);
        assert_eq!(spec.column_count, 3);
        assert_eq!(spec.row_count, 3);
        assert_eq!(spec.column_width, 11);
        assert_eq!(spec.row_height, 22);

        // 10 cells cannot fit on a 3x3 grid, so an extra column is added.
        let spec = GridSpec::new(10, 40, 30);
        assert_eq!(spec.column_count, 4);
        assert_eq!(spec.row_count, 3);
        assert_eq!(spec.column_width, 10);
        assert_eq!(spec.row_height, 10);

        // 12 cells can fit into the same 4x3 grids as required for 10 cells.
        let spec = GridSpec::new(12, 40, 30);
        assert_eq!(spec.column_count, 4);
        assert_eq!(spec.row_count, 3);
        assert_eq!(spec.column_width, 10);
        assert_eq!(spec.row_height, 10);

        // 13 cells cannot fit into a 4x3 grid.  Because there are already more columns than rows,
        // this time an additional row is added instead of a column, resulting in a 4x4 grid.
        let spec = GridSpec::new(13, 40, 40);
        assert_eq!(spec.column_count, 4);
        assert_eq!(spec.row_count, 4);
        assert_eq!(spec.column_width, 10);
        assert_eq!(spec.row_height, 10);

        // 16 cells can fit into the same 4x4 grid as required for 13 cells.
        let spec = GridSpec::new(16, 40, 40);
        assert_eq!(spec.column_count, 4);
        assert_eq!(spec.row_count, 4);
        assert_eq!(spec.column_width, 10);
        assert_eq!(spec.row_height, 10);

        // 17 cells cannot fit.  Because the number of rows and columns are the same, add another
        // column, as we did when going from 9 -> 10 cells (and unlike going from 12 -> 13 cells).
        let spec = GridSpec::new(17, 50, 40);
        assert_eq!(spec.column_count, 5);
        assert_eq!(spec.row_count, 4);
        assert_eq!(spec.column_width, 10);
        assert_eq!(spec.row_height, 10);
    }
}
