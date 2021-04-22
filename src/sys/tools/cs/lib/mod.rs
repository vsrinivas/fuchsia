// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod io;
pub mod v1;
pub mod v2;

use {
    crate::io::Directory,
    anyhow::{format_err, Error},
    fuchsia_async::TimeoutExt,
};

#[derive(Copy, Debug, Eq, PartialEq, Clone)]
pub enum Only {
    CMX,
    CML,
    Running,
    Stopped,
    All,
}

// TODO(66157): Implement FromArgValue for Only after cs is formally deprecated
// and move this to args.rs in ffx component list.
impl Only {
    pub fn from_string(only: &str) -> Result<Only, Error> {
        match only {
            "cmx" => Ok(Only::CMX),
            "cml" => Ok(Only::CML),
            "running" => Ok(Only::Running),
            "stopped" => Ok(Only::Stopped),
            _ => Err(format_err!("only should be 'cmx', 'cml', 'running', or 'stopped'.",)),
        }
    }
}

#[derive(Copy, Debug, Eq, PartialEq, Clone)]
pub enum Subcommand {
    List,
    Show,
    Select,
}

pub const CS_TREE_HELP: &str = "only format: 'cmx' / 'cml' / 'running' / 'stopped'.
Default option is displaying all components if no argument is entered.";

pub const CS_INFO_HELP: &str = "Filter format: component_name / url / partial url.

Example:
'appmgr', 'appmgr.cm', 'fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm'
will all return information about the appmgr component.";

pub static WIDTH_CS_TREE: usize = 19;
static CAPABILITY_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);

pub(crate) async fn get_capabilities(capability_dir: Directory) -> Vec<String> {
    let mut entries =
        capability_dir.entries().await.expect("Could not get entries from directory.");

    for (index, name) in entries.iter().enumerate() {
        if name == "svc" {
            entries.remove(index);
            let svc_dir = capability_dir.open_dir("svc").expect("open_dir(`svc`) failed!");
            let mut svc_entries =
                svc_dir.entries().await.expect("Could not get entries from directory.");
            entries.append(&mut svc_entries);
            break;
        }
    }

    entries.sort_unstable();
    entries
}

pub(crate) async fn get_capabilities_timeout(
    capability_dir: Directory,
) -> Result<Vec<String>, Error> {
    let mut entries = capability_dir
        .entries()
        .on_timeout(CAPABILITY_TIMEOUT, || Err(format_err!("Timeout occurred")))
        .await?;
    for (index, name) in entries.iter().enumerate() {
        if name == "svc" {
            entries.remove(index);
            let svc_dir = capability_dir.open_dir("svc").expect("open_dir(`svc`) failed!");
            let mut svc_entries =
                svc_dir.entries().await.expect("Could not get entries from directory.");
            entries.append(&mut svc_entries);
            break;
        }
    }

    entries.sort_unstable();
    Ok(entries)
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        std::fs::{self, File},
        tempfile::TempDir,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_capabilities_returns_capabilities() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- fuchsia.foo
        // |- hub
        // |- svc
        //    |- fuchsia.bar
        File::create(root.join("fuchsia.foo")).unwrap();
        File::create(root.join("hub")).unwrap();
        fs::create_dir(root.join("svc")).unwrap();
        File::create(root.join("svc/fuchsia.bar")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let capabilities = get_capabilities(root_dir).await;
        assert_eq!(
            capabilities,
            vec!["fuchsia.bar".to_string(), "fuchsia.foo".to_string(), "hub".to_string()]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_capabilities_timeout_returns_capabilities() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- fuchsia.foo
        // |- hub
        // |- svc
        //    |- fuchsia.bar
        File::create(root.join("fuchsia.foo")).unwrap();
        File::create(root.join("hub")).unwrap();
        fs::create_dir(root.join("svc")).unwrap();
        File::create(root.join("svc/fuchsia.bar")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let capabilities = get_capabilities_timeout(root_dir).await;
        assert_eq!(
            capabilities.unwrap(),
            vec!["fuchsia.bar".to_string(), "fuchsia.foo".to_string(), "hub".to_string()]
        );
    }
}
