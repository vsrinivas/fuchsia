// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    input_pipeline as input_pipeline_lib,
    input_pipeline::media_buttons_handler::MediaButtonsHandler,
};

pub fn create() -> Vec<Box<dyn input_pipeline_lib::input_handler::InputHandler>> {
    vec![make_media_buttons_handler()]
}

fn make_media_buttons_handler() -> Box<dyn input_pipeline_lib::input_handler::InputHandler> {
    Box::new(MediaButtonsHandler::new())
}
