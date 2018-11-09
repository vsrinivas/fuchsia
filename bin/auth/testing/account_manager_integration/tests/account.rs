// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use fidl_fuchsia_auth_account::{AccountManagerMarker, AccountManagerProxy, Status};
use fuchsia_async as fasync;
use futures::prelude::*;

fn proxy_test<TestFn, Fut>(test_fn: TestFn)
where
    TestFn: FnOnce(AccountManagerProxy) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let mut executor = fasync::Executor::new().expect("Failed to create executor");
    let proxy = fuchsia_app::client::connect_to_service::<AccountManagerMarker>()
        .expect("Failed to connect to account manager service");;

    executor
        .run_singlethreaded(test_fn(proxy))
        .expect("Executor run failed.")
}

#[test]
fn test_provision_new_account() {
    proxy_test(async move |account_manager| {
        assert_eq!(await!(account_manager.get_account_ids())?, vec![]);
        match await!(account_manager.provision_new_account())? {
            (Status::Ok, Some(new_account_id)) => {
                assert_eq!(
                    await!(account_manager.get_account_ids())?,
                    vec![*new_account_id]
                );
                Ok(())
            }
            (status, _) => Err(format_err!(
                "ProvisionNewAccount returned status: {:?}",
                status
            )),
        }
    });
}

// TODO(jsankey): Add additional tests as AccountManager and AccountHandler are connected.
