// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{ArchiveEntry, FarArchiveReader, FarListReader};
use anyhow::{Context, Result};
use ffx_package_far_args::ListSubCommand;
use ffx_writer::Writer;

use humansize::{file_size_opts, FileSize};
use prettytable::{cell, format::TableFormat, row, Row, Table};
use std::collections::HashMap;

pub fn list_impl(
    cmd: ListSubCommand,
    table_format: Option<TableFormat>,
    writer: &mut Writer,
) -> Result<()> {
    let mut archive_reader: Box<dyn FarListReader> = Box::new(FarArchiveReader::new(&cmd.archive)?);
    list_implementaion(cmd, table_format, writer, &mut archive_reader)
}

// internal implementation to allow injection of a mock
// archive reader.
fn list_implementaion(
    cmd: ListSubCommand,
    table_format: Option<TableFormat>,
    writer: &mut Writer,
    reader: &mut Box<dyn FarListReader>,
) -> Result<()> {
    let mut entries = read_file_entries(reader)?;

    // Sort the list and print.
    entries.sort();

    if writer.is_machine() {
        writer
            .machine(&entries)
            .context("writing machine representation of archive contents list")?;
    } else {
        print_list_table(&cmd, &entries, table_format, writer)
            .context("printing archive contents table")?;
    }
    Ok(())
}

pub(crate) fn read_file_entries(reader: &mut Box<dyn FarListReader>) -> Result<Vec<ArchiveEntry>> {
    // Create a map of hash to entry. This will be matched against
    // the file names to has in meta/contents.
    let mut blob_map: HashMap<String, ArchiveEntry> = HashMap::new();

    for b in reader.list_contents()? {
        // Map the entries to the ArchiveEntry struct which is serializable. Serialize
        // is used to support the machine readable interface.
        blob_map.insert(b.path.to_string(), b);
    }
    let mut entries: Vec<ArchiveEntry> = vec![];

    let (meta_list, meta_contents) = reader.list_meta_contents()?;

    entries.extend(meta_list);

    // Match the hash of the file from the contents list to a
    // blob entry in the archive. If it is missing, mark the length
    // and offset as zero.
    for (name, hash) in meta_contents {
        if let Some(mut blob) = blob_map.remove(&hash.to_string()) {
            blob.name = name.to_string();
            entries.push(blob);
        } else {
            entries.push(ArchiveEntry {
                name: name.to_string(),
                path: hash.to_string(),
                length: 0,
            });
        }
    }

    // After processing meta/contents, or if there is no meta.far in this archive,
    // there will be unreferenced blob entries listed, so add them to the
    // output.
    for (_, v) in blob_map.drain() {
        entries.push(v);
    }

    Ok(entries)
}

/// Print the list in a table.
fn print_list_table(
    cmd: &ListSubCommand,
    entries: &Vec<ArchiveEntry>,
    table_format: Option<TableFormat>,
    writer: &mut Writer,
) -> Result<()> {
    if entries.is_empty() {
        writer.line("")?;
        return Ok(());
    }
    let mut table = Table::new();
    let mut header = row!("NAME");
    if cmd.long_format {
        header.add_cell(cell!("PATH"));
        header.add_cell(cell!("LENGTH"));
    }
    table.set_titles(header);
    if let Some(fmt) = table_format {
        table.set_format(fmt);
    }

    for entry in entries {
        let mut row: Row = row![entry.name];

        if cmd.long_format {
            row.add_cell(cell!(entry.path));
            row.add_cell(cell!(entry
                .length
                .file_size(file_size_opts::CONVENTIONAL)
                .unwrap_or_else(|_| format!("{}b", entry.length))));
        }

        table.add_row(row);
    }
    table.print(writer)?;

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{test::create_mockreader, MockFarListReader};
    use ffx_writer::Format;
    use std::path::PathBuf;

    #[test]
    fn test_list_empty() -> Result<()> {
        let mut mockreader = MockFarListReader::new();
        mockreader.expect_list_contents().returning(|| Ok(vec![]));
        mockreader.expect_list_meta_contents().returning(|| Ok((vec![], HashMap::new())));

        let cmd = ListSubCommand { archive: PathBuf::from("some_empty.far"), long_format: false };

        let mut writer = Writer::new_test(None);
        let mut boxed_reader: Box<dyn FarListReader> = Box::from(mockreader);
        list_implementaion(cmd, None, &mut writer, &mut boxed_reader)?;

        assert_eq!(writer.test_output()?, "\n".to_string());
        Ok(())
    }

    #[test]
    /// Tests reading the "meta.far" directly vs. when part of a
    /// larger archive.
    fn test_list_with_no_meta() -> Result<()> {
        let mut mockreader = MockFarListReader::new();
        mockreader.expect_list_contents().returning(|| {
            Ok(vec![
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
            ])
        });
        mockreader.expect_list_meta_contents().returning(|| Ok((vec![], HashMap::new())));

        let cmd = ListSubCommand { archive: PathBuf::from("just_meta.far"), long_format: false };

        let mut writer = Writer::new_test(None);
        let mut boxed_reader: Box<dyn FarListReader> = Box::from(mockreader);
        list_implementaion(cmd, None, &mut writer, &mut boxed_reader)?;

        let expected = r#"
+-----------------------+
| NAME                  |
+=======================+
| meta/contents         |
+-----------------------+
| meta/package          |
+-----------------------+
| meta/the_component.cm |
+-----------------------+
"#[1..]
            .to_string();

        assert_eq!(writer.test_output()?, expected);
        Ok(())
    }

    #[test]
    fn test_list_with_meta() -> Result<()> {
        let mockreader = create_mockreader();

        let cmd = ListSubCommand { archive: PathBuf::from("just_meta.far"), long_format: false };

        let mut writer = Writer::new_test(None);
        let mut boxed_reader: Box<dyn FarListReader> = Box::from(mockreader);
        list_implementaion(cmd, None, &mut writer, &mut boxed_reader)?;

        let expected = r#"
+-----------------------+
| NAME                  |
+=======================+
| data/some_file        |
+-----------------------+
| lib/run.so            |
+-----------------------+
| meta.far              |
+-----------------------+
| meta/contents         |
+-----------------------+
| meta/package          |
+-----------------------+
| meta/the_component.cm |
+-----------------------+
| run_me                |
+-----------------------+
"#[1..]
            .to_string();

        assert_eq!(writer.test_output()?, expected);
        Ok(())
    }

    #[test]
    fn test_list_long_format() -> Result<()> {
        let mockreader = create_mockreader();

        let cmd = ListSubCommand { archive: PathBuf::from("just_meta.far"), long_format: true };

        let mut writer = Writer::new_test(None);
        let mut boxed_reader: Box<dyn FarListReader> = Box::from(mockreader);
        list_implementaion(cmd, None, &mut writer, &mut boxed_reader)?;

        let expected = r#"
+-----------------------+------------------------------------------------------------------+-----------+
| NAME                  | PATH                                                             | LENGTH    |
+=======================+==================================================================+===========+
| data/some_file        | 4ef082296b26108697e851e0b40f8d8d31f96f934d7076f3bad37d5103be172c | 292.97 KB |
+-----------------------+------------------------------------------------------------------+-----------+
| lib/run.so            | 892d655f2c841030d1b5556f9f124a753b5e32948471be76e72d330c6b6ba1db | 4 KB      |
+-----------------------+------------------------------------------------------------------+-----------+
| meta.far              | meta.far                                                         | 16 KB     |
+-----------------------+------------------------------------------------------------------+-----------+
| meta/contents         | meta/contents                                                    | 55 B      |
+-----------------------+------------------------------------------------------------------+-----------+
| meta/package          | meta/package                                                     | 25 B      |
+-----------------------+------------------------------------------------------------------+-----------+
| meta/the_component.cm | meta/the_component.cm                                            | 100 B     |
+-----------------------+------------------------------------------------------------------+-----------+
| run_me                | 1f487b576253664f9de1a940ad3a350ca47316b5cdb65254fbf267367fd77c62 | 4 KB      |
+-----------------------+------------------------------------------------------------------+-----------+
"#[1..].to_string();

        assert_eq!(writer.test_output()?, expected);
        Ok(())
    }

    #[test]
    fn test_list_machine() -> Result<()> {
        let mockreader = create_mockreader();

        let cmd = ListSubCommand { archive: PathBuf::from("just_meta.far"), long_format: false };

        let mut writer = Writer::new_test(Some(Format::JsonPretty));
        let mut boxed_reader: Box<dyn FarListReader> = Box::from(mockreader);
        list_implementaion(cmd, None, &mut writer, &mut boxed_reader)?;

        let expected = r#"
[
  {
    "name": "data/some_file",
    "path": "4ef082296b26108697e851e0b40f8d8d31f96f934d7076f3bad37d5103be172c",
    "length": 300000
  },
  {
    "name": "lib/run.so",
    "path": "892d655f2c841030d1b5556f9f124a753b5e32948471be76e72d330c6b6ba1db",
    "length": 4096
  },
  {
    "name": "meta.far",
    "path": "meta.far",
    "length": 16384
  },
  {
    "name": "meta/contents",
    "path": "meta/contents",
    "length": 55
  },
  {
    "name": "meta/package",
    "path": "meta/package",
    "length": 25
  },
  {
    "name": "meta/the_component.cm",
    "path": "meta/the_component.cm",
    "length": 100
  },
  {
    "name": "run_me",
    "path": "1f487b576253664f9de1a940ad3a350ca47316b5cdb65254fbf267367fd77c62",
    "length": 4096
  }
]"#[1..]
            .to_string();

        assert_eq!(writer.test_output()?, expected);
        Ok(())
    }
}
