// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Context as _, fidl_fuchsia_io as fio, fuchsia_zircon as zx, futures::Future};

/// Creates `temp_filename` under the `dir_proxy`, overwrite it if already exists, call the callback
/// with the opened file proxy, and then atomically rename it to `permanent_filename`.
pub async fn do_with_atomic_file<F>(
    dir_proxy: &fio::DirectoryProxy,
    temp_filename: &str,
    permanent_filename: &str,
    callback: impl FnOnce(fio::FileProxy) -> F,
) -> Result<(), anyhow::Error>
where
    F: Future<Output = Result<(), anyhow::Error>>,
{
    let file = fuchsia_fs::directory::open_file(
        dir_proxy,
        temp_filename,
        fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::CREATE | fio::OpenFlags::TRUNCATE,
    )
    .await
    .context("opening temp file")?;

    callback(Clone::clone(&file)).await.context("callback failed")?;

    let () = file
        .sync()
        .await
        .context("sending sync request")?
        .map_err(zx::Status::from_raw)
        .with_context(|| format!("syncing file: {}", temp_filename))?;
    fuchsia_fs::file::close(file).await.context("closing temp file")?;
    fuchsia_fs::directory::rename(dir_proxy, temp_filename, permanent_filename)
        .await
        .context("renaming temp file to permanent file")?;
    let () = dir_proxy
        .sync()
        .await
        .context("sending post-rename sync request")?
        .map_err(zx::Status::from_raw)
        .context("syncing directory")?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn test_do_with_atomic_file() {
        let dir = tempfile::tempdir().unwrap();
        let dir_proxy = fuchsia_fs::directory::open_in_namespace(
            dir.path().to_str().unwrap(),
            fio::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let temp_path = dir.path().join("foo.new");
        let permanent_path = dir.path().join("foo");

        do_with_atomic_file(&dir_proxy, "foo.new", "foo", |proxy| async move {
            fuchsia_fs::write_file(&proxy, "bar").await
        })
        .await
        .unwrap();
        assert_eq!(std::fs::read_to_string(&permanent_path).unwrap(), "bar");
        assert!(!temp_path.exists());

        // temp path should be overwritten
        std::fs::write(&temp_path, "garbage").unwrap();
        assert!(temp_path.exists());
        do_with_atomic_file(&dir_proxy, "foo.new", "foo", |proxy| async move {
            fuchsia_fs::write_file(&proxy, "baz").await
        })
        .await
        .unwrap();
        assert_eq!(std::fs::read_to_string(&permanent_path).unwrap(), "baz");
        assert!(!temp_path.exists());

        do_with_atomic_file(&dir_proxy, "foo.new", "foo", |proxy| async move {
            fuchsia_fs::write_file(&proxy, "qux").await
        })
        .await
        .unwrap();
        assert_eq!(std::fs::read_to_string(&permanent_path).unwrap(), "qux");
        assert!(!temp_path.exists());
    }
}
