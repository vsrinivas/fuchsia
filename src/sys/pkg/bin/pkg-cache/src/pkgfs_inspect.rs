// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fuchsia_inspect as finspect, fuchsia_syslog::fx_log_err,
    pkgfs::system::Client as SystemImage,
};

pub(crate) struct PkgfsInspectState {
    _exec_restrictions_enabled: finspect::StringProperty,
    _node: finspect::Node,
}

const INSPECT_EXEC_PROP_NAME: &str = "pkgfs-executability-restrictions-enabled";
const DISABLE_RESTRICTIONS_FILE_PATH: &str = "data/pkgfs_disable_executability_restrictions";

impl PkgfsInspectState {
    pub async fn new(
        system_image: impl FnOnce() -> Result<SystemImage, Error>,
        node: finspect::Node,
    ) -> Self {
        match Self::load_pkgfs_exec_restrictions_enabled(system_image).await {
            Ok(enabled) => Self {
                _exec_restrictions_enabled: node
                    .create_string(INSPECT_EXEC_PROP_NAME, format!("{}", enabled)),
                _node: node,
            },
            Err(e) => {
                fx_log_err!("Unexpected error trying to open file from system image: {:?}", e);
                Self {
                    _exec_restrictions_enabled: node.create_string(INSPECT_EXEC_PROP_NAME, "error"),
                    _node: node,
                }
            }
        }
    }

    async fn load_pkgfs_exec_restrictions_enabled(
        system_image: impl FnOnce() -> Result<SystemImage, Error>,
    ) -> Result<bool, Error> {
        let pkgfs_system = system_image()?;

        match pkgfs_system.open_file(DISABLE_RESTRICTIONS_FILE_PATH).await {
            Ok(_) => Ok(false),
            Err(pkgfs::system::SystemImageFileOpenError::NotFound) => Ok(true),
            // We expect this to get zx::Status::NOT_SUPPORTED in tests that
            // haven't actually addded system image package to pkgfs.
            Err(other) => Err(other.into()),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::anyhow,
        fuchsia_async as fasync,
        fuchsia_inspect::assert_inspect_tree,
        std::fs::{create_dir_all, File},
        tempfile::TempDir,
    };

    struct TestPkgfs {
        pkgfs_root: TempDir,
    }

    impl TestPkgfs {
        fn new_with_restrictions_enabled(restrictions_enabled: bool) -> Self {
            let pkgfs_root = TempDir::new().unwrap();
            create_dir_all(pkgfs_root.path().join("system/data")).unwrap();
            if !restrictions_enabled {
                File::create(
                    pkgfs_root.path().join(format!("system/{}", DISABLE_RESTRICTIONS_FILE_PATH)),
                )
                .unwrap();
            }
            Self { pkgfs_root }
        }

        fn system_image(&self) -> SystemImage {
            SystemImage::open_from_pkgfs_root(&fidl_fuchsia_io::DirectoryProxy::new(
                fasync::Channel::from_channel(
                    fdio::transfer_fd(File::open(self.pkgfs_root.path()).unwrap()).unwrap().into(),
                )
                .unwrap(),
            ))
            .unwrap()
        }
    }

    async fn assert_pkgfs_executability_restrictions_enabled(
        system_image: impl FnOnce() -> Result<SystemImage, Error>,
        expected_state: String,
    ) {
        let inspector = finspect::Inspector::new();

        let _pkgfs_inspect =
            PkgfsInspectState::new(system_image, inspector.root().create_child("pkgfs")).await;

        assert_inspect_tree!(inspector, root: {
            "pkgfs": {
                INSPECT_EXEC_PROP_NAME.to_string() => expected_state,
            }
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_pkgfs_versions_restrictions_enabled() {
        let env = TestPkgfs::new_with_restrictions_enabled(true);

        assert_pkgfs_executability_restrictions_enabled(
            || Ok(env.system_image()),
            "true".to_string(),
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_pkgfs_versions_restrictions_disabled() {
        let env = TestPkgfs::new_with_restrictions_enabled(false);

        assert_pkgfs_executability_restrictions_enabled(
            || Ok(env.system_image()),
            "false".to_string(),
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_pkgfs_versions_restrictions_error() {
        assert_pkgfs_executability_restrictions_enabled(
            || Err(anyhow!("failed to get system image")),
            "error".to_string(),
        )
        .await;
    }
}
