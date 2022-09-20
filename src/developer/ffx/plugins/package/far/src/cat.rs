// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use errors::ffx_bail;
use ffx_package_archive_utils::{read_file_entries, FarArchiveReader, FarListReader};
use ffx_package_far_args::CatSubCommand;

pub fn cat_impl<W: std::io::Write>(cmd: CatSubCommand, writer: &mut W) -> Result<()> {
    let mut archive_reader: Box<dyn FarListReader> = Box::new(FarArchiveReader::new(&cmd.archive)?);

    cat_implementation(cmd, writer, &mut archive_reader)
}

fn cat_implementation<W: std::io::Write>(
    cmd: CatSubCommand,
    writer: &mut W,
    reader: &mut Box<dyn FarListReader>,
) -> Result<()> {
    let file_name = cmd.far_path.to_string_lossy();

    let entries = read_file_entries(reader)?;
    if let Some(entry) =
        entries.iter().find(|x| if cmd.as_hash { x.path == file_name } else { x.name == file_name })
    {
        let data = reader.read_entry(entry)?;
        writer.write_all(&data)?;
    } else {
        ffx_bail!("file {} not found in {}", file_name, cmd.archive.to_string_lossy());
    }

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use ffx_package_archive_utils::{
        test_utils::{create_mockreader, test_contents, BLOB1, LIB_RUN_SO_BLOB, LIB_RUN_SO_PATH},
        MockFarListReader,
    };
    use std::path::PathBuf;

    #[test]
    fn test_cat_blob() -> Result<()> {
        let cmd = CatSubCommand {
            archive: PathBuf::from("some.far"),
            far_path: PathBuf::from(BLOB1),
            as_hash: true,
        };

        let mut output: Vec<u8> = vec![];

        let mock_reader: MockFarListReader = create_mockreader();

        let expected = test_contents(BLOB1);

        let mut boxed_reader: Box<dyn FarListReader> = Box::from(mock_reader);

        cat_implementation(cmd, &mut output, &mut boxed_reader)?;
        assert_eq!(expected, output);

        Ok(())
    }

    #[test]
    fn test_cat_filename() -> Result<()> {
        let cmd = CatSubCommand {
            archive: PathBuf::from("some.far"),
            far_path: PathBuf::from(LIB_RUN_SO_PATH),
            as_hash: false,
        };

        let mut output: Vec<u8> = vec![];

        let mock_reader: MockFarListReader = create_mockreader();

        let expected = test_contents(LIB_RUN_SO_BLOB);

        let mut boxed_reader: Box<dyn FarListReader> = Box::from(mock_reader);

        cat_implementation(cmd, &mut output, &mut boxed_reader)?;
        assert_eq!(expected, output);

        Ok(())
    }
}
