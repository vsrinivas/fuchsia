// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use errors::ResultExt;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let result = ffx_command::run().await;
    if let Err(err) = &result {
        let mut out = std::io::stderr();
        // abort hard on a failure to print the user error somehow
        errors::write_result(err, &mut out).unwrap();
        ffx_command::report_user_error(err).await.unwrap();
        ffx_config::print_log_hint(&mut out).await;
    }
    std::process::exit(result.exit_code());
}
