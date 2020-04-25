// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

use crate::tiles::types::{
    AddTileInput, AddTileOutput, ListTileOutput, RemoveTileInput, TileOutput,
};
use fidl_fuchsia_developer_tiles::ControllerMarker;
use fuchsia_component::client::{launch, launcher, App};
use fuchsia_syslog::macros::fx_log_info;
use serde_json::{from_value, to_value, Value};
use std::cell::RefCell;

/// Facade providing access to fuchsia.developer.tiles interfaces.
pub struct TilesFacade {
    tiles: RefCell<Option<App>>,
}

impl std::fmt::Debug for TilesFacade {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("TilesFacade").finish()
    }
}

impl TilesFacade {
    pub fn new() -> TilesFacade {
        TilesFacade { tiles: RefCell::new(None) }
    }

    /// Starts tiles.cmx component.
    pub fn start_tile(&self) -> Result<Value, Error> {
        if self.tiles.borrow().is_none() {
            match launch(
                &launcher()?,
                String::from("fuchsia-pkg://fuchsia.com/tiles#meta/tiles.cmx"),
                None,
            ) {
                Ok(a) => {
                    self.tiles.replace(Some(a));
                    return Ok(to_value(TileOutput::Success)?);
                }
                Err(err) => {
                    return Err(format_err!("Starting Tiles component failed with err {:?}", err))
                }
            }
        }
        return Ok(to_value(TileOutput::Success)?);
    }

    /// Asks the tiles component to quit.
    pub fn stop_tile(&self) -> Result<Value, Error> {
        if self.tiles.borrow().is_some() {
            let controller_proxy =
                self.tiles.borrow().as_ref().unwrap().connect_to_service::<ControllerMarker>()?;
            controller_proxy.quit()?;
            self.tiles.replace(None);
        }
        return Ok(to_value(TileOutput::Success)?);
    }

    /// Adds a tile by url, optionally user can also specify args and focus.
    ///
    /// params format:
    /// {
    ///     "url": "full package url",
    ///     "allowed_focus": true/false,
    ///     "args": ["arg1", "arg2"]
    /// }
    /// Example:
    /// {
    ///     "url": "fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx"
    /// }
    pub async fn add_from_url(&self, args: Value) -> Result<Value, Error> {
        let add_request: AddTileInput = from_value(args)?;
        fx_log_info!("Add tile request received {:?}", add_request);
        self.start_tile()?;
        let controller_proxy =
            self.tiles.borrow().as_ref().unwrap().connect_to_service::<ControllerMarker>()?;
        match add_request {
            AddTileInput { url, allow_focus, args } => {
                let focus = match allow_focus {
                    Some(f) => f,
                    None => false,
                };
                let argv = match args {
                    Some(x) => x,
                    None => vec![],
                };
                let arg_strs: Vec<&str> = argv.iter().map(|a| a.as_str()).collect();
                let key = controller_proxy
                    .add_tile_from_url(&url, focus, Some(&mut arg_strs.into_iter()))
                    .await?;
                let return_value = to_value(AddTileOutput::new(key))?;
                return Ok(return_value);
            }
        }
    }

    /// Remove a tile by its key id from the current tiles.cmx realm.
    ///
    /// params format:
    /// {
    ///     "key": "key number",
    /// }
    ///
    /// Example:
    /// {
    ///     "key": "1",
    /// }
    pub async fn remove(&self, args: Value) -> Result<Value, Error> {
        let remove_request: RemoveTileInput = from_value(args)?;
        fx_log_info!("Remove tile request received {:?}", remove_request);
        self.start_tile()?;
        let controller_proxy =
            self.tiles.borrow().as_ref().unwrap().connect_to_service::<ControllerMarker>()?;
        let key =
            remove_request.key.parse::<u32>().map_err(|_| anyhow!("key must be an integer"))?;
        match controller_proxy.remove_tile(key) {
            Ok(_) => Ok(to_value(TileOutput::Success)?),
            Err(err) => Err(format_err!("Remove tiles failed with err {:?}", err)),
        }
    }

    /// List tiles from the current tiles.cmx realm.
    ///
    /// Returns the tiles key, url, and focuse values in arrays.
    pub async fn list(&self) -> Result<Value, Error> {
        self.start_tile()?;
        let controller_proxy =
            self.tiles.borrow().as_ref().unwrap().connect_to_service::<ControllerMarker>()?;
        let (keys, urls, _sizes, focuses) = controller_proxy.list_tiles().await?;
        let return_value = to_value(ListTileOutput::new(&keys, &urls, &focuses))?;
        return Ok(return_value);
    }
}
