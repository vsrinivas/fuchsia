// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) mod wisdom_server_impl;

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_examples_intl_wisdom as fwisdom, fuchsia_async as fasync,
    fuchsia_component::server,
    futures::{StreamExt, TryStreamExt},
    icu_data,
};

fn main() -> Result<(), Error> {
    // [START loader_example]
    // Force the loading of ICU data at the beginning of the program.  Since
    // Fuchsia's ICU does not have `libicudata.so`, we must load the data here
    // so that locales could be used in the server.
    let _loader = icu_data::Loader::new()?;
    // [END loader_example]

    let mut executor = fasync::LocalExecutor::new().context("error creating executor")?;
    let mut fs = server::ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            run_service(stream).await.expect("failed to run service");
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

async fn run_service(mut stream: fwisdom::IntlWisdomServer_RequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        let fwisdom::IntlWisdomServer_Request::AskForWisdom {
            intl_profile,
            timestamp_ms,
            responder,
        } = event;
        let response = wisdom_server_impl::ask_for_wisdom(&intl_profile, timestamp_ms)?;
        responder.send(Some(&response)).context("failed to send")?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn basic() {
        assert!(true);
    }
}
