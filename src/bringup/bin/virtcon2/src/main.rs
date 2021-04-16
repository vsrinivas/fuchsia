// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{App, AppAssistantPtr, AppContext, AssistantCreatorFunc, LocalBoxFuture},
    virtual_console_lib::VirtualConsoleAppAssistant,
};

fn make_app_assistant_fut(
    _app_context: &AppContext,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(VirtualConsoleAppAssistant::new());
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

pub fn make_app_assistant() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut)
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant())
}
