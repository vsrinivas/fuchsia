// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ext4_read_only::{
        parser::Parser,
        readers::VmoReader,
        structs::{self, MIN_EXT4_SIZE},
    },
    fidl_fuchsia_mem::Buffer,
    fuchsia_vfs_pseudo_fs_mt::directory::entry::DirectoryEntry,
    fuchsia_zircon::Status,
    std::sync::Arc,
};

#[derive(Debug, PartialEq)]
pub enum ConstructFsError {
    VmoReadError(Status),
    ParsingError(structs::ParsingError),
}

pub fn construct_fs(source: Buffer) -> Result<Arc<dyn DirectoryEntry>, ConstructFsError> {
    if source.size < MIN_EXT4_SIZE as u64 {
        // Too small to even fit the first copy of the ext4 Super Block.
        ConstructFsError::VmoReadError(Status::NO_SPACE);
    }

    let mut parser = Parser::new(VmoReader::new(Arc::new(source)));

    match parser.build_fuchsia_tree() {
        Ok(tree) => Ok(tree),
        Err(e) => Err(ConstructFsError::ParsingError(e)),
    }
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
        ext4_read_only::structs::MIN_EXT4_SIZE,
        fidl_fuchsia_io::{
            DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN, OPEN_FLAG_DESCRIBE,
            OPEN_RIGHT_READABLE,
        },
        fuchsia_vfs_pseudo_fs_mt::directory::test_utils::{
            run_server_client, DirentsSameInodeBuilder,
        },
        fuchsia_zircon::Vmo,
        std::fs,
    };

    #[test]
    fn image_too_small() {
        let vmo = Vmo::create(10).expect("VMO is created");
        vmo.write(b"too small", 0).expect("VMO write() succeeds");
        let buffer = fidl_fuchsia_mem::Buffer { vmo: vmo, size: 10 };

        assert!(construct_fs(buffer).is_err(), "Expected failed parsing of VMO.");
    }

    #[test]
    fn invalid_fs() {
        let vmo = Vmo::create(MIN_EXT4_SIZE as u64).expect("VMO is created");
        vmo.write(b"not ext4", 0).expect("VMO write() succeeds");
        let buffer = fidl_fuchsia_mem::Buffer { vmo: vmo, size: MIN_EXT4_SIZE as u64 };

        assert!(construct_fs(buffer).is_err(), "Expected failed parsing of VMO.");
    }

    #[test]
    fn list_root() {
        let data = fs::read("/pkg/data/nest.img").expect("Unable to read file");
        let vmo = Vmo::create(data.len() as u64).expect("VMO is created");
        vmo.write(data.as_slice(), 0).expect("VMO write() succeeds");
        let buffer = fidl_fuchsia_mem::Buffer { vmo: vmo, size: data.len() as u64 };

        let tree = construct_fs(buffer).expect("construct_fs parses the vmo");

        run_server_client(OPEN_RIGHT_READABLE, tree, |root| async move {
            {
                let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
                expected.add(DIRENT_TYPE_DIRECTORY, b".");
                expected.add(DIRENT_TYPE_FILE, b"file1");
                expected.add(DIRENT_TYPE_DIRECTORY, b"inner");
                expected.add(DIRENT_TYPE_DIRECTORY, b"lost+found");

                assert_read_dirents!(root, 1000, expected.into_vec());
            }

            let flags = OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE;
            let compare = "file1 contents.\n";
            open_as_file_assert_content!(&root, flags, "file1", compare);

            assert_close!(root);
        });
    }
}
