// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod io;
pub mod v1;
pub mod v2;

use {crate::io::Directory, fuchsia_async::TimeoutExt, futures::future::FutureExt};

static CAPABILITY_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);

pub(crate) async fn get_capabilities(capability_dir: Directory) -> Option<Vec<String>> {
    let entries =
        capability_dir.entries().map(|d| Some(d)).on_timeout(CAPABILITY_TIMEOUT, || None).await;
    if let Some(mut entries) = entries {
        for (index, name) in entries.iter().enumerate() {
            if name == "svc" {
                entries.remove(index);
                let svc_dir = capability_dir.open_dir("svc");
                let mut svc_entries = svc_dir.entries().await;
                entries.append(&mut svc_entries);
                break;
            }
        }

        entries.sort_unstable();
        Some(entries)
    } else {
        // Can't get capabilities because the directory could not be opened.
        // This is probably because it isn't being served.
        None
    }
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

        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let capabilities = get_capabilities(root_dir).await;
        assert_eq!(
            capabilities,
            Some(vec!["fuchsia.bar".to_string(), "fuchsia.foo".to_string(), "hub".to_string()])
        );
    }
}
