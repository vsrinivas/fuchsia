// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_component::client::connect_to_protocol,
    input_pipeline as input_pipeline_lib,
    input_pipeline_lib::{
        media_buttons_handler::MediaButtonsHandler, touch_injector_handler::TouchInjectorHandler,
        Size,
    },
};

pub async fn create(
    media_buttons_handler: MediaButtonsHandler,
) -> Vec<Box<dyn input_pipeline_lib::input_handler::InputHandler>> {
    let scenic = connect_to_protocol::<fidl_fuchsia_ui_scenic::ScenicMarker>()
        .expect("Failed to connect to Scenic.");
    vec![Box::new(media_buttons_handler), make_touch_injector_handler(&scenic).await]
}

async fn make_touch_injector_handler(
    scenic: &fidl_fuchsia_ui_scenic::ScenicProxy,
) -> Box<dyn input_pipeline_lib::input_handler::InputHandler> {
    let display_info = scenic.get_display_info().await.expect("Failed to get display info.");
    let display_size =
        Size { width: display_info.width_in_px as f32, height: display_info.height_in_px as f32 };

    let handler = TouchInjectorHandler::new(display_size)
        .await
        .expect("Failed to create TouchInjectorHandler.");
    Box::new(handler)
}
