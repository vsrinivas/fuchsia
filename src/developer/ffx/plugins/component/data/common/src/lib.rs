// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    std::path::{Component, PathBuf},
};

pub const REMOTE_PATH_HELP: &'static str = "Remote paths have the following format:\n\n\
[component instance ID]::[path relative to storage]\n\n\
`..` is not valid anywhere in the remote path.";

pub struct RemotePath {
    pub component_instance_id: String,
    pub relative_path: PathBuf,
}

impl RemotePath {
    pub fn parse(input: &str) -> Result<Self> {
        match input.split_once("::") {
            Some((first, second)) => {
                if second.contains("::") {
                    ffx_bail!(
                        "Remote path must contain exactly one `::` separator. {}",
                        REMOTE_PATH_HELP
                    )
                }

                let component_instance_id = first.to_string();
                let relative_path = PathBuf::from(second);

                // Path checks (ignore `.`) (no `..`, `/` or prefix allowed)
                let mut normalized_relative_path = PathBuf::new();
                for component in relative_path.components() {
                    match component {
                        Component::Normal(c) => normalized_relative_path.push(c),
                        Component::RootDir => continue,
                        Component::CurDir => continue,
                        c => ffx_bail!("Unsupported path object: {:?}. {}", c, REMOTE_PATH_HELP),
                    }
                }

                Ok(Self { component_instance_id, relative_path: normalized_relative_path })
            }
            None => ffx_bail!(
                "Remote path must contain exactly one `::` separator. {}",
                REMOTE_PATH_HELP
            ),
        }
    }
}
