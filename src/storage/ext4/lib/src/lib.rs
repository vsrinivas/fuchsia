// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_vfs_pseudo_fs_mt::{
        directory::entry::DirectoryEntry,
        file::vmo::{asynchronous::NewVmo, read_only},
        pseudo_directory,
    },
    fuchsia_zircon::{Status, Vmo, VmoChildOptions},
    std::sync::Arc,
};

#[derive(Debug)]
pub enum ConstructFsError {
    VmoReadError(Status),
}

pub fn construct_fs(source: Vmo) -> Result<Arc<dyn DirectoryEntry>, ConstructFsError> {
    let size = source.get_size().map_err(ConstructFsError::VmoReadError)?;

    let source = Arc::new(source);

    Ok(pseudo_directory! {
        "img" => read_only(move || {
            let source = source.clone();
            async move {
                Ok(NewVmo {
                    vmo: source.create_child(VmoChildOptions::COPY_ON_WRITE, 0, size)?,
                    size,
                    capacity: size,
                })
            }
        }),
    })
}

#[cfg(test)]
mod tests {
    use super::construct_fs;

    // macros
    use fuchsia_vfs_pseudo_fs_mt::{
        assert_close, assert_event, assert_read, assert_read_dirents, open_as_file_assert_content,
        open_get_file_proxy_assert_ok, open_get_proxy_assert,
    };

    use {
        fidl_fuchsia_io::{
            DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN, OPEN_FLAG_DESCRIBE,
            OPEN_RIGHT_READABLE,
        },
        fuchsia_vfs_pseudo_fs_mt::directory::test_utils::{
            run_server_client, DirentsSameInodeBuilder,
        },
        fuchsia_zircon::Vmo,
    };

    #[test]
    fn list_root() {
        let vmo = Vmo::create(10).expect("VMO is created");
        vmo.write(b"list root", 0).expect("VMO write() succeeds");

        let tree = construct_fs(vmo).expect("construct_fs parses the vmo");

        run_server_client(OPEN_RIGHT_READABLE, tree, |root| {
            async move {
                {
                    let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                    expected.add(DIRENT_TYPE_DIRECTORY, b".").add(DIRENT_TYPE_FILE, b"img");

                    assert_read_dirents!(root, 1000, expected.into_vec());
                }

                let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
                open_as_file_assert_content!(&root, flags, "img", "list root");

                assert_close!(root);
            }
        });
    }
}
