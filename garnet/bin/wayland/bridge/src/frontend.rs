// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use failure::Error;
use fidl::endpoints::{create_proxy, ClientEnd};
use fidl_fuchsia_developer_tiles as tiles;
use fidl_fuchsia_ui_app::ViewProviderMarker;
use fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy};
use fuchsia_app::client::{connect_to_service, App, Launcher};
use fuchsia_async as fasync;
use fuchsia_scenic as scenic;
use futures::prelude::*;

/// A |ViewSink| is the component that the bridge can push new views into so
/// that they can be presented.
pub trait ViewSink: Send + Sync {
    /// The bridge has generated a new "toplevel" view. We model these as
    /// scenic ViewProvider instances that can be used to create a view
    /// representing the surface.
    ///
    /// Compositor objects that create top level surfaces should pass the
    /// `ViewProvider` to the `ViewSink` so that they can be presented.
    fn new_view_provider(&self, view_provider: ClientEnd<ViewProviderMarker>);

    /// Gets a reference to the Scenic service.
    fn scenic(&self) -> &ScenicProxy;

    /// Gets a pointer to the scenic session. This enables components to create
    /// scenic resources that can be consumed by views crated by the view
    /// manager.
    fn scenic_session(&self) -> scenic::SessionPtr;
}
pub type ViewSinkPtr = Arc<Box<dyn ViewSink>>;

/// A simple |ViewSink| that spawns a new 'tiles' process and pushes view
/// providers directly to the tiles instance.
///
/// Note that launching tiles will set a new root view and each bridge process
/// will create a new tiles instance. This component is intended for development
/// purposes.
pub struct TileViewSink {
    /// We need to retain this to keep the tiles process alive.
    _app: App,
    /// The tiles controller interface.
    tiles_controller: tiles::ControllerProxy,

    /// A connection to the 'Scenic' service.
    scenic: ScenicProxy,
    /// A ref-counted pointer to a scenic 'session'.
    scenic_session: scenic::SessionPtr,
}

impl TileViewSink {
    /// Creates a new `TilesViewSink`.
    ///
    /// As part of creating a `TilesViewSink`, a new `tiles` process will be
    /// launched.
    pub fn new() -> Result<ViewSinkPtr, Error> {
        // Connect to Scenic
        let scenic = connect_to_service::<ScenicMarker>()?;
        let (session_proxy, session_request) = create_proxy()?;
        scenic.create_session(session_request, None)?;
        let scenic_session = scenic::Session::new(session_proxy);

        // Spawn a tiles process. We'll forward our |ViewProvider|s here to be
        // presented.
        let launcher = Launcher::new()?;
        let app =
            launcher.launch("fuchsia-pkg://fuchsia.com/tiles#meta/tiles.cmx".to_string(), None)?;
        let tiles_controller = app.connect_to_service(tiles::ControllerMarker)?;
        Ok(Arc::new(Box::new(TileViewSink { _app: app, scenic, scenic_session, tiles_controller })))
    }
}

impl ViewSink for TileViewSink {
    fn new_view_provider(&self, view_provider: ClientEnd<ViewProviderMarker>) {
        let fut = self.tiles_controller.add_tile_from_view_provider("tile", view_provider);
        fasync::spawn_local(fut.map_ok(|_| ()).unwrap_or_else(|e| println!("Failed {:?}", e)));
    }

    fn scenic(&self) -> &ScenicProxy {
        &self.scenic
    }

    fn scenic_session(&self) -> scenic::SessionPtr {
        self.scenic_session.clone()
    }
}
