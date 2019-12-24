// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fuchsia_async as fasync, hub_report::HubReport};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let hub_report = HubReport::new()?;

    // Read the listing of entries of the hub rooted at this component and
    // pass the results to the integration test.
    hub_report.report_directory_contents("/hub").await?;

    // Read the listing of the children of the parent from its hub, and pass the
    // results to the integration test.
    hub_report.report_directory_contents("/parent_hub/children").await?;

    // Read the content of the resolved_url file in the sibling hub, and pass the
    // results to the integration test.
    hub_report.report_file_content("/sibling_hub/exec/resolved_url").await?;

    Ok(())
}
