// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{App, AppAssistantPtr},
    fidl_fuchsia_boot::ArgumentsMarker,
    fuchsia_async::LocalExecutor,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_trace_provider,
    virtual_console_lib::{VirtualConsoleAppAssistant, VirtualConsoleArgs},
};

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let init = async {
        // Redirect standard out to debuglog.
        stdout_to_debuglog::init().await?;

        // Connect to boot arguments service.
        let boot_args = connect_to_protocol::<ArgumentsMarker>()?;

        // Get boot argument.
        VirtualConsoleArgs::new_with_proxy(boot_args).await
    };

    let args = {
        let mut executor = LocalExecutor::new().expect("Failed to create executor");
        executor.run_singlethreaded(init)?
    };

    // Early out if virtcon should be disabled.
    if args.disable {
        println!("vc: disabled");
        return Ok(());
    }

    println!("vc: started with args {:?}", args);

    App::run(Box::new(|app_context| {
        let f = async move {
            let assistant = Box::new(VirtualConsoleAppAssistant::new(app_context, args)?);
            Ok::<AppAssistantPtr, Error>(assistant)
        };
        Box::pin(f)
    }))
}
