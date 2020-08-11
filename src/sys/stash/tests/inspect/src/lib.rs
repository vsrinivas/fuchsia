// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async::{self as fasync},
    fuchsia_component::client::{launcher, AppBuilder},
    fuchsia_inspect::testing::assert_inspect_tree,
    fuchsia_inspect_contrib::reader::{ArchiveReader, Inspect},
};

const STASH_URL: &str = "fuchsia-pkg://fuchsia.com/stash-integration-tests#meta/stash.cmx";
const SECURE_STASH_URL: &str =
    "fuchsia-pkg://fuchsia.com/stash-integration-tests#meta/stash_secure.cmx";

#[fasync::run_singlethreaded(test)]
async fn stash_inspect() -> Result<(), Error> {
    let launcher = launcher()?;
    let _stash = AppBuilder::new(STASH_URL).spawn(&launcher)?;

    let data = ArchiveReader::new().add_selector("stash.cmx:root").snapshot::<Inspect>().await?;

    assert_eq!(1, data.len());

    assert_inspect_tree!(data[0].payload.as_ref().unwrap(),
        root: contains {
            secure_mode: false,
            "fuchsia.inspect.Health": contains {
                status: "OK"
            }
        }
    );
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn stash_secure_inspect() -> Result<(), Error> {
    let launcher = launcher()?;
    let _stash = AppBuilder::new(SECURE_STASH_URL).spawn(&launcher)?;

    let data =
        ArchiveReader::new().add_selector("stash_secure.cmx:root").snapshot::<Inspect>().await?;

    assert_eq!(1, data.len());

    assert_inspect_tree!(data[0].payload.as_ref().unwrap(),
        root: contains {
            secure_mode: true,
            "fuchsia.inspect.Health": contains {
                status: "OK"
            }
        }
    );

    Ok(())
}
