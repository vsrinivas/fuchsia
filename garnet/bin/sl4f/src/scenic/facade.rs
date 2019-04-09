// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::scenic::types::ScreenshotDataDef;
use failure::{Error, ResultExt};
use fidl_fuchsia_ui_app::{ViewConfig, ViewMarker, ViewProviderMarker};
use fidl_fuchsia_ui_policy::PresenterMarker;
use fidl_fuchsia_ui_scenic::ScenicMarker;
use fuchsia_component::{
    self as app,
    client::{launch, launcher},
};
use fuchsia_scenic as scenic;
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

    pub async fn present_view(&self, url: String, config: Option<ViewConfig>) -> Result<(), Error> {
        let presenter = app::client::connect_to_service::<PresenterMarker>()
            .expect("failed to connect to root presenter");

        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch(&launcher, url, None)?;

        let (view_token, mut view_holder_token) = scenic::new_view_token_pair()?;

        // (for now) gate v1/v2 on the presence of a view config
        match config {
            Some(mut config) => {
                // v2
                let view = app.connect_to_service(ViewMarker)?;
                view.set_config(&mut config)?;
                view.attach(view_token.value)?;
            }
            None => {
                // v1
                let view_provider = app.connect_to_service(ViewProviderMarker)?;
                view_provider.create_view(view_token.value, None, None)?;
            }
        }

        presenter.present_view(&mut view_holder_token, None)?;

        app.controller().detach()?;
        Ok(())
    }
}
