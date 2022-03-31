// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_test as ftest, fidl_fuchsia_io as fio,
    fuchsia_zircon::{self as zx, HandleBased},
    std::{collections::HashMap, sync::Arc},
    tracing::*,
    vfs::{
        directory::entry::DirectoryEntry,
        directory::helper::DirectlyMutable,
        directory::immutable::simple as simpledir,
        execution_scope::ExecutionScope,
        file::vmo::asynchronous::{read_only, NewVmo},
        path::Path as VfsPath,
    },
};

enum DirectoryOrFile {
    Directory(HashMap<String, DirectoryOrFile>),
    File(Arc<zx::Vmo>, u64),
}

/// An `ExecutionScope` does not terminate tasks scheduled on it when the scope is dropped, as many
/// copies of the same `ExecutionScope` can be created. To ensure that cleanup happens at the right
/// time, this struct will call `self.execution_scope.shutdown()` when dropped, causing any tasks
/// running within this scope to stop executing.
struct ExecutionScopeDropper {
    execution_scope: ExecutionScope,
}

impl Drop for ExecutionScopeDropper {
    fn drop(&mut self) {
        self.execution_scope.shutdown();
    }
}

pub async fn read_only_directory(
    directory_name: String,
    directory_contents: Arc<ftest::DirectoryContents>,
    outgoing_dir: ServerEnd<fio::DirectoryMarker>,
) {
    if let Err(e) =
        read_only_directory_helper(directory_name, directory_contents, outgoing_dir).await
    {
        error!("unable to provide read only directory: {:?}", e);
    }
}

pub async fn read_only_directory_helper(
    directory_name: String,
    directory_contents: Arc<ftest::DirectoryContents>,
    outgoing_dir: ServerEnd<fio::DirectoryMarker>,
) -> Result<(), anyhow::Error> {
    let directory_hashmap = directory_contents_to_hashmap(directory_contents)?;
    let directory = build_directory(directory_hashmap)?;
    let execution_scope_dropper = ExecutionScopeDropper { execution_scope: ExecutionScope::new() };
    let top_directory = simpledir::simple();
    top_directory.clone().add_entry(&directory_name, directory)?;
    top_directory.clone().set_not_found_handler(Box::new(move |path| {
        warn!("nonexistent path {:?} accessed in read only directory {:?}", path, directory_name);
    }));
    top_directory.open(
        execution_scope_dropper.execution_scope.clone(),
        fio::OPEN_RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        VfsPath::dot(),
        outgoing_dir.into_channel().into(),
    );
    execution_scope_dropper.execution_scope.wait().await;
    Ok(())
}

fn directory_contents_to_hashmap(
    directory_contents: Arc<ftest::DirectoryContents>,
) -> Result<HashMap<String, DirectoryOrFile>, Error> {
    let mut directory_hashmap = HashMap::new();
    for entry in directory_contents.entries.iter() {
        let mut current_directory = &mut directory_hashmap;
        let mut path_parts_iter = entry.file_path.split('/').peekable();
        while let Some(path_part) = path_parts_iter.next() {
            if path_parts_iter.peek().is_some() {
                let dir_or_file = current_directory
                    .entry(path_part.to_string())
                    .or_insert(DirectoryOrFile::Directory(HashMap::new()));
                current_directory = match dir_or_file {
                    DirectoryOrFile::Directory(d) => d,
                    DirectoryOrFile::File(_, _) => {
                        return Err(format_err!(
                            "directory_contents invalid, {:?} is inside of a file",
                            entry.file_path
                        ))
                    }
                };
            } else {
                let vmo =
                    Arc::new(entry.file_contents.vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?);
                current_directory.insert(
                    path_part.to_string(),
                    DirectoryOrFile::File(vmo, entry.file_contents.size),
                );
            }
        }
    }
    Ok(directory_hashmap)
}

fn build_directory(
    input: HashMap<String, DirectoryOrFile>,
) -> Result<Arc<simpledir::Simple>, Error> {
    let directory = simpledir::simple();
    for (path, directory_or_file) in input.into_iter() {
        match directory_or_file {
            DirectoryOrFile::Directory(sub_directory) => directory
                .clone()
                .add_entry(&path, build_directory(sub_directory)?)
                .context("could not add directory to directory")?,
            DirectoryOrFile::File(vmo, size) => {
                directory
                    .clone()
                    .add_entry(
                        &path,
                        read_only(move || {
                            let vmo = vmo.clone();
                            async move {
                                Ok(NewVmo {
                                    vmo: vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?,
                                    size,
                                    capacity: size,
                                })
                            }
                        }),
                    )
                    .context("could not add file to directory")?;
            }
        }
    }
    Ok(directory)
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy, fidl_fuchsia_mem as fmem, files_async,
        fuchsia_async as fasync, futures::TryStreamExt, io_util, maplit::hashset,
        std::collections::HashSet,
    };

    #[fuchsia::test]
    async fn read_only_directory_contains_expected_files() {
        let directory_name = "directory-capability-name".to_string();
        fn create_file_contents(contents: &str) -> fmem::Buffer {
            let vmo = zx::Vmo::create(contents.len() as u64).expect("failed to create vmo");
            vmo.write(contents.as_bytes(), 0).expect("failed to write to vmo");
            fmem::Buffer { vmo, size: contents.len() as u64 }
        }
        let directory_contents = ftest::DirectoryContents {
            entries: vec![
                ftest::DirectoryEntry {
                    file_path: "config/example.json".to_string(),
                    file_contents: create_file_contents("example file contents"),
                },
                ftest::DirectoryEntry {
                    file_path: "a/b/c/d/e/f".to_string(),
                    file_contents: create_file_contents("g"),
                },
                ftest::DirectoryEntry {
                    file_path: "hippos".to_string(),
                    file_contents: create_file_contents("rule!"),
                },
            ],
        };

        let (outgoing_dir_proxy, outgoing_dir_server_end) = create_proxy().unwrap();

        let _directory_task = fasync::Task::local(read_only_directory(
            directory_name.clone(),
            Arc::new(directory_contents),
            outgoing_dir_server_end,
        ));

        let directory_filenames: HashSet<_> =
            files_async::readdir_recursive(&outgoing_dir_proxy, None)
                .map_ok(|dir_entry| dir_entry.name)
                .try_collect()
                .await
                .expect("failed to read directory");
        assert_eq!(
            directory_filenames,
            hashset! {
                "directory-capability-name/config/example.json".to_string(),
                "directory-capability-name/a/b/c/d/e/f".to_string(),
                "directory-capability-name/hippos".to_string(),
            },
        );

        let open_and_read_file = move |file_path: &'static str| {
            let outgoing_dir_proxy = Clone::clone(&outgoing_dir_proxy);
            async move {
                let file_proxy = io_util::directory::open_file(
                    &outgoing_dir_proxy,
                    file_path,
                    io_util::OPEN_RIGHT_READABLE,
                )
                .await
                .expect(&format!("failed to open file {:?}", file_path));
                io_util::read_file(&file_proxy)
                    .await
                    .expect(&format!("failed to read file {:?}", file_path))
            }
        };

        assert_eq!(
            open_and_read_file("directory-capability-name/config/example.json").await,
            "example file contents".to_string(),
        );
        assert_eq!(
            open_and_read_file("directory-capability-name/a/b/c/d/e/f").await,
            "g".to_string(),
        );
        assert_eq!(
            open_and_read_file("directory-capability-name/hippos").await,
            "rule!".to_string(),
        );
    }
}
