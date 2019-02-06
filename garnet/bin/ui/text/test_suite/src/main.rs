// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
use failure::{err_msg, Error, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_ui_text as txt;
use fidl_fuchsia_ui_text_testing as txt_testing;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_async::TimeoutExt;
use fuchsia_zircon::{DurationNum, Time};
use futures::future::FutureObj;
use futures::prelude::*;
use lazy_static::lazy_static;

lazy_static! {
    static ref TEST_TIMEOUT: Time = 10.seconds().after_now();
}

macro_rules! text_field_tests {
    ($list:ident: $($test_fn:ident),*) => {
        static $list: &'static [(&'static str, fn(&txt::TextFieldProxy) -> FutureObj<Result<(), Error>>)] = &[
            $( (stringify!($test_fn), move |text_field| {
                FutureObj::new(Box::new($test_fn(text_field)))
            }) ),*
        ];
    };
}

text_field_tests! {
    TEST_FNS:
    test_sends_initial_state,
    test_noop_causes_state_update
}

fn main() -> Result<(), Error> {
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating fuchsia_async executor for text tests failed")?;
    let done = ServicesServer::new()
        .add_service((txt_testing::TextFieldTestSuiteMarker::NAME, move |chan| {
            bind_text_tester(chan);
        }))
        .start()
        .context("Creating ServicesServer for text tester service failed")?;
    executor
        .run_singlethreaded(done)
        .context("Attempt to start up IME services on async::Executor failed")?;
    Ok(())
}

fn bind_text_tester(chan: fuchsia_async::Channel) {
    fasync::spawn(
        async move {
            let mut stream = txt_testing::TextFieldTestSuiteRequestStream::from_channel(chan);
            while let Some(msg) = await!(stream.try_next())
                .expect("error reading value from IME service request stream")
            {
                match msg {
                    txt_testing::TextFieldTestSuiteRequest::RunTest {
                        field,
                        test_id,
                        responder,
                    } => {
                        let (passed, message) = await!(run_test(
                            field.into_proxy().expect("failed to convert ClientEnd to proxy"),
                            test_id
                        ));
                        responder
                            .send(passed, &message)
                            .expect("failed to send response to RunTest");
                    }
                    txt_testing::TextFieldTestSuiteRequest::ListTests { responder } => {
                        responder
                            .send(&mut list_tests().iter_mut())
                            .expect("failed to send response to ListTests");
                    }
                }
            }
        },
    );
}

async fn run_test(text_field: txt::TextFieldProxy, test_id: u64) -> (bool, String) {
    match TEST_FNS.get(test_id as usize) {
        Some((_test_name, test_fn)) => {
            let res = await!(test_fn(&text_field));
            let passed = res.is_ok();
            let msg = match res {
                Ok(()) => format!("passed"),
                Err(e) => format!("{}", e),
            };
            (passed, msg)
        }
        None => (false, format!("unknown test id: {}", test_id)),
    }
}

fn list_tests() -> Vec<txt_testing::TestInfo> {
    TEST_FNS
        .iter()
        .enumerate()
        .map(|(i, (test_name, _test_fn))| txt_testing::TestInfo {
            id: i as u64,
            name: test_name.to_string(),
        })
        .collect()
}

async fn get_update(text_field: &txt::TextFieldProxy) -> Result<txt::TextFieldState, Error> {
    let mut stream = text_field.take_event_stream();
    let msg_future = stream
        .try_next()
        .map_err(|e| err_msg(format!("{}", e)))
        .on_timeout(*TEST_TIMEOUT, || Err(err_msg("Waiting for on_update event timed out")));
    let msg = await!(msg_future)?.ok_or(err_msg("TextMgr event stream unexpectedly closed"))?;
    match msg {
        txt::TextFieldEvent::OnUpdate { state, .. } => Ok(state),
    }
}

async fn test_sends_initial_state(text_field: &txt::TextFieldProxy) -> Result<(), Error> {
    let _state = await!(get_update(text_field))?;
    Ok(())
}

async fn test_noop_causes_state_update(text_field: &txt::TextFieldProxy) -> Result<(), Error> {
    let state = await!(get_update(text_field))?;

    text_field.begin_edit(state.revision)?;
    if await!(text_field.commit_edit())? != txt::TextError::Ok {
        return Err(err_msg("Expected commit_edit to succeed"));
    }

    let _state = await!(get_update(text_field))?;
    Ok(())
}
