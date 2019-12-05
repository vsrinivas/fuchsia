// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_mem::Buffer,
    fuchsia_vfs_pseudo_fs_mt::{
        directory::entry::DirectoryEntry,
        file::vmo::{asynchronous::NewVmo, read_only},
        pseudo_directory,
    },
    fuchsia_zircon::{Status, VmoChildOptions},
    std::sync::Arc,
};

// Any smaller would not even fit the first copy of the ext4 Super Block.
const MIN_EXT4_SIZE: u64 = 2048;

#[derive(Debug)]
pub enum ConstructFsError {
    VmoReadError(Status),
}

pub fn construct_fs(source: Buffer) -> Result<Arc<dyn DirectoryEntry>, ConstructFsError> {
    let size = source.size;
    if size < MIN_EXT4_SIZE {
        ConstructFsError::VmoReadError(Status::NO_SPACE);
    }

    let vmo = Arc::new(source.vmo);

    Ok(pseudo_directory! {
        "img" => read_only(move || {
            let vmo = vmo.clone();
            async move {
                Ok(NewVmo {
                    vmo: vmo.create_child(VmoChildOptions::COPY_ON_WRITE, 0, size)?,
                    size,
                    capacity: size,
                })
            }
        }),
    })
}

#[cfg(test)]
mod tests {
    use super::{construct_fs, MIN_EXT4_SIZE};

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
        let vmo = Vmo::create(MIN_EXT4_SIZE).expect("VMO is created");
        vmo.write(b"list root", 0).expect("VMO write() succeeds");
        let buffer = fidl_fuchsia_mem::Buffer { vmo: vmo, size: MIN_EXT4_SIZE };

        let tree = construct_fs(buffer).expect("construct_fs parses the vmo");

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
