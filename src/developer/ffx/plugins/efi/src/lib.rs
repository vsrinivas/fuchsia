// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    fatfs,
    ffx_core::ffx_plugin,
    ffx_efi_args::{EfiCommand, EfiSubcommand},
    std::{
        cmp::max,
        collections::HashMap,
        fs::{File, OpenOptions},
        io::{prelude::*, BufReader},
        path::Path,
    },
};

fn format(volume: &File, size: u64) -> Result<()> {
    volume.set_len(size)?;

    let mut label = [b' '; 11];
    label[0..3].copy_from_slice("ESP".as_bytes());

    // Note: there is no way to specify OEM label, which was set to "Fuchsia" before and now it is
    // "MSWIN4.1".
    let options = fatfs::FormatVolumeOptions::new()
        .bytes_per_sector(512)
        .fat_type(fatfs::FatType::Fat32)
        .volume_label(label);

    fatfs::format_volume(volume, options)?;
    Ok(())
}

fn compute_size(files: &HashMap<&str, Vec<u8>>) -> u64 {
    // min_size is the minimum image size
    let min_size = 1024 * 1024;
    // Fudge factor (percentage) to account for filesystem metadata.
    let fudge_factor = 10;
    // sectors per track is 63, and a sector is 512, so we must round to the nearest
    // 32256.
    let size_alignment = 63 * 512;

    let mut total: u64 = 0;
    for (_name, content) in files {
        total += content.len() as u64;
    }

    total += total * fudge_factor / 100;

    // Ensure total is larger than min_size
    total = max(min_size, total);

    // Align to the legacy track size
    total = (total + size_alignment - 1) / size_alignment * size_alignment;
    return total;
}

fn copy_files(volume: &File, files: &HashMap<&str, Vec<u8>>) -> Result<()> {
    let fs = fatfs::FileSystem::new(volume, fatfs::FsOptions::new())?;

    for (dest_path, content) in files.iter() {
        let mut dir = fs.root_dir();
        let mut path_elements: Vec<&str> = dest_path.split("/").collect();
        let filename = path_elements.pop().unwrap();
        for dirname in path_elements {
            dir = dir.create_dir(dirname)?;
        }
        let mut dstfile = dir.create_file(filename)?;
        dstfile.write_all(&content)?;
    }

    Ok(())
}

fn load_files<'a>(
    names: &'a HashMap<String, String>,
    files: &mut HashMap<&'a str, Vec<u8>>,
) -> Result<()> {
    for (dest_path, source_path) in names.iter() {
        let f = match File::open(&Path::new(source_path)) {
            Ok(f) => f,
            Err(err) => ffx_bail!("Failed to copy {}: {}", source_path, err),
        };
        let mut buffer = Vec::new();
        BufReader::new(f).read_to_end(&mut buffer)?;
        files.insert(dest_path, buffer);
    }
    Ok(())
}

#[ffx_plugin()]
pub async fn command(cmd: EfiCommand) -> Result<()> {
    match &cmd.subcommand {
        EfiSubcommand::Create(create) => {
            let output_path = Path::new(&create.output);

            let mut names = HashMap::new();
            let mut maybe_insert = |dest_path: &str, value: &Option<String>| {
                value.as_ref().map(|v| names.insert(String::from(dest_path), String::from(v)));
            };
            maybe_insert("zircon.bin", &create.zircon);
            maybe_insert("bootdata.bin", &create.bootdata);
            maybe_insert("EFI/BOOT/BOOTX64.EFI", &create.efi_bootloader);
            maybe_insert("zedboot.bin", &create.zedboot);
            maybe_insert("cmdline", &create.cmdline);

            let mut files = HashMap::new();
            load_files(&names, &mut files)?;

            // Some arguments require special handling: cmdline, efi_bootloader
            if create.efi_bootloader.is_some() {
                files
                    .insert("EFI/Google/GSetup/Boot", "efi\\boot\\bootx64.efi".as_bytes().to_vec());
            }

            let size = compute_size(&files);

            let volume =
                OpenOptions::new().read(true).write(true).create(true).open(output_path)?;
            format(&volume, size)?;
            copy_files(&volume, &files)
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        ffx_efi_args::{CreateCommand, EfiCommand, EfiSubcommand},
        std::fs::metadata,
        tempfile::tempdir,
    };

    fn set_file_content(name: &str, content: &str) -> Result<()> {
        let mut file = File::create(name)?;
        file.write_all(content.as_bytes())?;
        Ok(())
    }

    fn check_file_content(output: &str, path: &str, content: &str) -> Result<()> {
        // TODO(vol): can pass File, to this function, but fatfs crashes if seek pos != 0
        let volume = File::open(output)?;
        // TODO(vol): can pass FileSystem to the function if I can figure how to pass type
        // parameters. Write trait seems like required for FileSystem, but File doesn't
        // privide it.
        let fs = fatfs::FileSystem::new(volume, fatfs::FsOptions::new())?;
        let mut file = fs.root_dir().open_file(path)?;
        let mut buf = vec![];
        file.read_to_end(&mut buf)?;
        assert_eq!(String::from_utf8_lossy(&buf), content);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_empty() -> Result<()> {
        let tmpdir = tempdir()?;
        let output = tmpdir.path().join("test_output").to_str().unwrap().to_string();

        let result = command(EfiCommand {
            subcommand: EfiSubcommand::Create(CreateCommand {
                output: output.clone(),
                cmdline: None,
                bootdata: None,
                efi_bootloader: None,
                zircon: None,
                zedboot: None,
            }),
        })
        .await
        .unwrap();
        assert_eq!(result, ());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create() -> Result<()> {
        let tmpdir = tempdir()?;
        let tmppath = |name| tmpdir.path().join(name).to_str().unwrap().to_string();

        let cmdline = tmppath("test_cmdline");
        let cmdline_content = "cmdline content";
        let output = tmppath("test_output");
        let bootloader = tmppath("test_bootloader");
        let bootloader_content = "bootloader content".repeat(100);
        let bootdata = tmppath("test_bootdata");
        let bootdata_content = "bootdata content";
        let zircon = tmppath("test_zircon");
        let zircon_content = "zircon content";
        let zedboot = tmppath("test_zedboot");
        let zedboot_content = "zedboot content";

        set_file_content(&cmdline, cmdline_content)?;
        set_file_content(&bootloader, &bootloader_content)?;
        set_file_content(&bootdata, bootdata_content)?;
        set_file_content(&zircon, zircon_content)?;
        set_file_content(&zedboot, zedboot_content)?;

        let result = command(EfiCommand {
            subcommand: EfiSubcommand::Create(CreateCommand {
                output: output.clone(),
                cmdline: Some(cmdline),
                bootdata: Some(bootdata),
                efi_bootloader: Some(bootloader),
                zircon: Some(zircon),
                zedboot: Some(zedboot),
            }),
        })
        .await
        .unwrap();
        assert_eq!(result, ());

        check_file_content(&output, "EFI/BOOT/BOOTX64.EFI", &bootloader_content)?;
        check_file_content(&output, "cmdline", cmdline_content)?;
        check_file_content(&output, "bootdata.bin", bootdata_content)?;
        check_file_content(&output, "zircon.bin", zircon_content)?;
        check_file_content(&output, "zedboot.bin", zedboot_content)?;
        let output_size = metadata(output)?.len();

        // Check that produced image is at least 1MiB
        assert!(output_size >= 1024 * 1024, "size = {}", output_size);

        // Check that the image is aligned to legacy track size.
        assert_eq!(output_size % 63 * 512, 0);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_create_worst_case_reservation() -> Result<()> {
        let tmpdir = tempdir()?;
        let tmppath = |name| tmpdir.path().join(name).to_str().unwrap().to_string();

        // Worst case of overhead for image exactly 1MiB
        let size = 1024 * 1024 * 100 / 105;
        let output = tmppath("test_output");
        let bootloader = tmppath("test_bootloader");
        let bootloader_content = "b".repeat(size);

        set_file_content(&bootloader, &bootloader_content)?;

        let result = command(EfiCommand {
            subcommand: EfiSubcommand::Create(CreateCommand {
                output: output.clone(),
                cmdline: None,
                bootdata: None,
                efi_bootloader: Some(bootloader),
                zircon: None,
                zedboot: None,
            }),
        })
        .await
        .unwrap();
        assert_eq!(result, ());

        check_file_content(&output, "EFI/BOOT/BOOTX64.EFI", &bootloader_content)?;
        let output_size = metadata(output)?.len();

        // Check that produced image is at least 1MiB
        assert!(output_size > 1024 * 1024, "size = {}", output_size);

        // Check that the image is aligned to legacy track size.
        assert_eq!(output_size % 63 * 512, 0);

        Ok(())
    }
}
