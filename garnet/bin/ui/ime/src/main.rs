// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
mod fidl_helpers;
mod ime_service;
mod index_convert;
mod legacy_ime;

use failure::{Error, ResultExt};
use fidl::endpoints::DiscoverableService;
use fidl_fuchsia_ui_input::{ImeServiceMarker, ImeVisibilityServiceMarker};
use fidl_fuchsia_ui_text::TextInputContextMarker;
use fuchsia_app::server::ServicesServer;
use fuchsia_syslog;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["ime_service"]).expect("ime syslog init should not fail");
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating fuchsia_async executor for IME service failed")?;
    let ime_service = ime_service::ImeService::new();
    let ime_service1 = ime_service.clone();
    let ime_service2 = ime_service.clone();
    let ime_service3 = ime_service.clone();
    let done = ServicesServer::new()
        .add_service((ImeServiceMarker::NAME, move |chan| {
            ime_service1.bind_ime_service(chan);
        }))
        .add_service((ImeVisibilityServiceMarker::NAME, move |chan| {
            ime_service2.bind_ime_visibility_service(chan);
        }))
        .add_service((TextInputContextMarker::NAME, move |chan| {
            ime_service3.bind_text_input_context(chan);
        }))
        .start()
        .context("Creating ServicesServer for IME service failed")?;
    executor
        .run_singlethreaded(done)
        .context("Attempt to start up IME services on async::Executor failed")?;

    Ok(())
}

#[cfg(test)]
mod test {
    use fidl_fuchsia_ui_input as uii;
    use fidl_fuchsia_ui_text as txt;
    use fidl_fuchsia_ui_text_testing as txt_testing;
    use fuchsia_app::client::Launcher;
    use futures::prelude::*;

    #[test]
    fn test_external_text_field_implementation() {
        fuchsia_syslog::init_with_tags(&["ime_service"]).expect("ime syslog init should not fail");
        let mut executor = fuchsia_async::Executor::new()
            .expect("Creating fuchsia_async executor for IME service failed");
        let launcher = Launcher::new().expect("Failed to open launcher service");
        let app = launcher
            .launch(
                "fuchsia-pkg://fuchsia.com/text_test_suite#meta/test_suite.cmx".to_string(),
                None,
            )
            .expect("Failed to launch testing service");
        let tester = app
            .connect_to_service(txt_testing::TextFieldTestSuiteMarker)
            .expect("Failed to connect to testing service");
        let done = (async move || {
            let mut passed = true;
            let test_list = await!(tester.list_tests()).expect("Failed to get list of tests");
            for test in test_list {
                if let Err(e) = await!(run_test(&tester, test.id)) {
                    passed = false;
                    eprintln!("[ FAIL ] {}\n{}", test.name, e);
                } else {
                    eprintln!("[  ok  ] {}", test.name);
                }
            }
            if !passed {
                panic!("Text integration tests failed");
            }
        })();
        executor.run_singlethreaded(done);
    }

    async fn run_test(
        tester: &txt_testing::TextFieldTestSuiteProxy,
        test_id: u64,
    ) -> Result<(), String> {
        let mut ime_service = crate::ime_service::ImeService::new();
        let (text_proxy, server_end) =
            fidl::endpoints::create_proxy::<txt::TextInputContextMarker>()
                .expect("Failed to create proxy");
        let (imec_client, _imec_server) =
            fidl::endpoints::create_endpoints::<uii::InputMethodEditorClientMarker>()
                .expect("Failed to create endpoints");
        let (_ime_client, ime_server) =
            fidl::endpoints::create_endpoints::<uii::InputMethodEditorMarker>()
                .expect("Failed to create endpoints");
        let chan = fuchsia_async::Channel::from_channel(server_end.into_channel())
            .expect("Failed to create channel");
        ime_service.bind_text_input_context(chan);
        await!(ime_service.get_input_method_editor(
            uii::KeyboardType::Text,
            uii::InputMethodAction::Done,
            crate::fidl_helpers::default_state(),
            imec_client,
            ime_server,
        ));
        let mut stream = text_proxy.take_event_stream();
        let msg = await!(stream.try_next())
            .expect("Failed to get event.")
            .expect("TextInputContext event stream unexpectedly closed.");
        let text_field = match msg {
            txt::TextInputContextEvent::OnFocus { text_field, .. } => text_field,
            _ => panic!("Expected text_field to pass OnFocus event type"),
        };
        let (passed, msg) = await!(tester.run_test(text_field, test_id))
            .expect("Call to text testing service failed");
        if passed {
            Ok(())
        } else {
            Err(msg)
        }
    }
}
