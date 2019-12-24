// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test_helpers;
mod tests;

use crate::test_helpers::TextFieldWrapper;
use crate::tests::*;
use anyhow::{Context as _, Error};
use fidl_fuchsia_ui_text as txt;
use fidl_fuchsia_ui_text_testing as txt_testing;
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon::DurationNum;
use futures::future::FutureObj;
use futures::prelude::*;
use lazy_static::lazy_static;

lazy_static! {
    pub static ref TEST_TIMEOUT: fasync::Time = 10.seconds().after_now();
}

macro_rules! text_field_tests {
    ($list:ident: $($test_fn:ident),*) => {
        static $list: &'static [(&'static str, fn(&mut TextFieldWrapper) -> FutureObj<'_, Result<(), Error>>)] = &[
            $( (stringify!($test_fn), move |wrapper| {
                FutureObj::new(Box::new($test_fn(wrapper)))
            }) ),*
        ];
    };
}

text_field_tests! {
    TEST_FNS:
    test_noop_causes_state_update,
    test_simple_content_request,
    test_multibyte_unicode_content_request,
    test_multiple_edit_moves_points,
    test_invalid_delete_off_end_of_field
}

fn main() -> Result<(), Error> {
    let mut executor = fuchsia_async::Executor::new()
        .context("Creating fuchsia_async executor for text tests failed")?;
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(bind_text_tester);
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

fn bind_text_tester(mut stream: txt_testing::TextFieldTestSuiteRequestStream) {
    fasync::spawn(async move {
        while let Some(msg) =
            stream.try_next().await.expect("error reading value from IME service request stream")
        {
            match msg {
                txt_testing::TextFieldTestSuiteRequest::RunTest { field, test_id, responder } => {
                    let res = run_test(
                        field.into_proxy().expect("failed to convert ClientEnd to proxy"),
                        test_id,
                    )
                    .await;
                    let (ok, message) = match res {
                        Ok(()) => (true, format!("passed")),
                        Err(e) => (false, e),
                    };
                    responder.send(ok, &message).expect("failed to send response to RunTest");
                }
                txt_testing::TextFieldTestSuiteRequest::ListTests { responder } => {
                    responder
                        .send(&mut list_tests().iter_mut())
                        .expect("failed to send response to ListTests");
                }
            }
        }
    });
}

async fn run_test(text_field: txt::TextFieldProxy, test_id: u64) -> Result<(), String> {
    let mut wrapper = TextFieldWrapper::new(text_field).await.map_err(|e| format!("{}", e))?;
    let res = match TEST_FNS.get(test_id as usize) {
        Some((_test_name, test_fn)) => test_fn(&mut wrapper).await,
        None => return Err(format!("unknown test id: {}", test_id)),
    };
    res.map_err(|e| format!("{}", e))
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
