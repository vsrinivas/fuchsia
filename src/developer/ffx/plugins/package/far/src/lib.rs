// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, bail, Result};
use ffx_core::ffx_plugin;
pub use ffx_package_far_args::{
    CatSubCommand, ExtractSubCommand, FarCommand, FarSubCommand, ListSubCommand,
};
use ffx_writer::Writer;
use fuchsia_archive::{Error, Utf8Entry, Utf8Reader};
use fuchsia_hash::Hash;
use fuchsia_pkg::MetaContents;
use serde::Serialize;
use std::{collections::HashMap, fs::File, io::Cursor, path::PathBuf};

#[cfg(test)]
use mockall::automock;

mod cat;
mod extract;
mod list;

#[derive(Clone, Debug, PartialEq, PartialOrd, Ord, Eq, Serialize)]
pub(crate) struct ArchiveEntry {
    pub name: String,
    pub path: String,
    pub length: u64,
}

impl From<Utf8Entry<'_>> for ArchiveEntry {
    fn from(entry: Utf8Entry<'_>) -> Self {
        ArchiveEntry {
            name: entry.path().to_string(),
            path: entry.path().to_string(),
            length: entry.length(),
        }
    }
}

/// Trait for listing the contents of the package archive. This
/// enables mocking of the reader for testing.
#[cfg_attr(test, automock)]
pub(crate) trait FarListReader {
    fn list_contents(&self) -> Result<Vec<ArchiveEntry>>;
    fn list_meta_contents(&mut self) -> Result<(Vec<ArchiveEntry>, HashMap<String, Hash>)>;
    fn read_entry(&mut self, entry: &ArchiveEntry) -> Result<Vec<u8>>;
}

/// Struct to implement the FarListReader by using the fuchsia_archive library.
pub(crate) struct FarArchiveReader {
    archive: Utf8Reader<File>,
}

impl FarArchiveReader {
    pub(crate) fn new(archive_name: &PathBuf) -> Result<FarArchiveReader> {
        Ok(FarArchiveReader {
            archive: fuchsia_archive::Utf8Reader::new(File::open(archive_name)?)?,
        })
    }

    fn read_from_meta(&mut self, file_name: &str) -> Result<Vec<u8>> {
        let meta_far_blob = self.archive.read_file("meta.far")?;
        let meta_cursor = Cursor::new(meta_far_blob);
        let mut meta_archive = fuchsia_archive::Utf8Reader::new(meta_cursor)?;
        meta_archive.read_file(file_name).map_err(|e| anyhow!("{}", e))
    }
}

impl FarListReader for FarArchiveReader {
    fn list_contents(&self) -> Result<Vec<ArchiveEntry>> {
        return Ok(self.archive.list().map(|e| e.into()).collect());
    }

    fn list_meta_contents(&mut self) -> Result<(Vec<ArchiveEntry>, HashMap<String, Hash>)> {
        let meta_entries: Vec<ArchiveEntry>;
        let meta_contents: HashMap<String, Hash>;

        let (meta_entries, meta_contents) = match self.archive.read_file("meta.far") {
            Ok(meta_far_blob) => {
                let meta_cursor = Cursor::new(meta_far_blob);
                let mut meta_archive = fuchsia_archive::Utf8Reader::new(meta_cursor)?;
                meta_entries = meta_archive.list().map(|e| e.into()).collect();

                let contents_blob = meta_archive.read_file("meta/contents")?;
                meta_contents =
                    MetaContents::deserialize(contents_blob.as_slice())?.into_contents();
                (meta_entries, meta_contents)
            }
            Err(Error::PathNotPresent(_)) => (vec![], HashMap::new()),
            Err(e) => bail!("Error reading meta.far: {}", e),
        };
        Ok((meta_entries, meta_contents))
    }

    fn read_entry(&mut self, entry: &ArchiveEntry) -> Result<Vec<u8>> {
        let contents = match self.archive.read_file(&entry.path) {
            Ok(data) => data,
            Err(Error::PathNotPresent(p)) => {
                // Check if path starts with meta
                if entry.path.starts_with("meta/") {
                    self.read_from_meta(&entry.path)?
                } else {
                    bail!(Error::PathNotPresent(p));
                }
            }
            Err(e) => bail!("{}", e),
        };
        Ok(contents)
    }
}

#[ffx_plugin("ffx_package")]
pub async fn cmd_far(
    cmd: FarCommand,
    #[ffx(machine = Vec<T:Serialize>)] mut writer: Writer,
) -> Result<()> {
    match cmd.subcommand {
        FarSubCommand::List(subcmd) => list::list_impl(subcmd, None, &mut writer),
        FarSubCommand::Cat(subcmd) => cat::cat_impl(subcmd, &mut std::io::stdout()),
        FarSubCommand::Extract(subcmd) => extract::extract_impl(subcmd, &mut writer),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_hash::Hash;
    use std::str::FromStr;

    pub(crate) static BLOB1: &str =
        "1f487b576253664f9de1a940ad3a350ca47316b5cdb65254fbf267367fd77c62";
    pub(crate) static RUN_ME_BLOB: &str = BLOB1;
    pub(crate) static RUN_ME_PATH: &str = "run_me";
    pub(crate) static BLOB2: &str =
        "892d655f2c841030d1b5556f9f124a753b5e32948471be76e72d330c6b6ba1db";
    pub(crate) static LIB_RUN_SO_BLOB: &str = BLOB2;
    pub(crate) static LIB_RUN_SO_PATH: &str = "lib/run.so";
    pub(crate) static BLOB3: &str =
        "4ef082296b26108697e851e0b40f8d8d31f96f934d7076f3bad37d5103be172c";
    pub(crate) static DATA_SOME_FILE_BLOB: &str = BLOB3;
    pub(crate) static DATA_SOME_FILE_PATH: &str = "data/some_file";

    pub(crate) fn create_mockreader() -> MockFarListReader {
        let mut mockreader = MockFarListReader::new();
        mockreader.expect_list_contents().returning(|| {
            Ok(vec![
                ArchiveEntry { name: BLOB1.to_string(), path: BLOB1.to_string(), length: 1024 * 4 },
                ArchiveEntry { name: BLOB2.to_string(), path: BLOB2.to_string(), length: 1024 * 4 },
                ArchiveEntry { name: BLOB3.to_string(), path: BLOB3.to_string(), length: 300000 },
                ArchiveEntry {
                    name: "meta.far".to_string(),
                    path: "meta.far".to_string(),
                    length: 1024 * 16,
                },
            ])
        });
        mockreader.expect_list_meta_contents().returning(|| {
            Ok((
                vec![
                    ArchiveEntry {
                        name: "meta/the_component.cm".to_string(),
                        path: "meta/the_component.cm".to_string(),
                        length: 100,
                    },
                    ArchiveEntry {
                        name: "meta/package".to_string(),
                        path: "meta/package".to_string(),
                        length: 25,
                    },
                    ArchiveEntry {
                        name: "meta/contents".to_string(),
                        path: "meta/contents".to_string(),
                        length: 55,
                    },
                ],
                HashMap::from([
                    (RUN_ME_PATH.to_string(), Hash::from_str(RUN_ME_BLOB)?),
                    (LIB_RUN_SO_PATH.to_string(), Hash::from_str(LIB_RUN_SO_BLOB)?),
                    (DATA_SOME_FILE_PATH.to_string(), Hash::from_str(DATA_SOME_FILE_BLOB)?),
                ]),
            ))
        });

        mockreader.expect_read_entry().returning(|entry| {
            let ret = test_contents(&entry.path);
            match ret.len() {
                0 => bail!("Zero length content for {:?}", entry),
                _ => Ok(ret),
            }
        });
        mockreader
    }

    pub(crate) fn test_contents(value: &str) -> Vec<u8> {
        format!("Contents for {}", value).into_bytes()
    }
}
