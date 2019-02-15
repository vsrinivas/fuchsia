// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::scenic::types::ScreenshotDataDef;
use failure::Error;
use fidl_fuchsia_ui_scenic::ScenicMarker;
use fuchsia_app as app;
use serde_json::{to_value, Value};

/// Perform Scenic operations.
///
/// Note this object is shared among all threads created by server.
///
/// This facade does not hold onto a Scenic proxy as the server may be
/// long-running while individual tests set up and tear down Scenic.
#[derive(Debug)]
pub struct ScenicFacade {}

impl ScenicFacade {
    pub fn new() -> ScenicFacade {
        ScenicFacade {}
    }

    pub async fn take_screenshot(&self) -> Result<Value, Error> {
        let scenic =
            app::client::connect_to_service::<ScenicMarker>().expect("failed to connect to Scenic");

        let (screenshot, success) = await!(scenic.take_screenshot())?;
        if success {
            Ok(to_value(ScreenshotDataDef::new(screenshot))?)
        } else {
            bail!("TakeScreenshot failed")
        }
    }
}
