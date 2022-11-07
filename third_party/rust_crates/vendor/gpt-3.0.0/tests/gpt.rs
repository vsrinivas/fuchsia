use gpt;

use gpt::disk;
use std::collections::BTreeMap;
use std::convert::TryFrom;
use std::io::{SeekFrom, Write};
use std::path;
use tempfile::NamedTempFile;

#[test]
fn test_gptconfig_empty() {
    let tempdisk = NamedTempFile::new().expect("failed to create tempfile disk");
    let cfg = {
        let c1 = gpt::GptConfig::new();
        let c2 = gpt::GptConfig::default();
        assert_eq!(c1, c2);
        c1
    };

    let lb_size = disk::LogicalBlockSize::Lb4096;
    let disk = cfg
        .initialized(false)
        .logical_block_size(lb_size)
        .open(tempdisk.path())
        .unwrap();
    assert_eq!(*disk.logical_block_size(), lb_size);
    assert_eq!(disk.primary_header(), None);
    assert_eq!(disk.backup_header(), None);
    assert!(disk.partitions().is_empty());
}

#[test]
fn test_gptdisk_linux_01() {
    let diskpath = path::Path::new("tests/fixtures/gpt-linux-disk-01.img");
    let lb_size = disk::LogicalBlockSize::Lb512;

    let gdisk = gpt::GptConfig::new().open(diskpath).unwrap();
    assert_eq!(*gdisk.logical_block_size(), lb_size);
    assert!(gdisk.primary_header().is_some());
    assert!(gdisk.backup_header().is_some());
    assert_eq!(gdisk.partitions().len(), 1);

    let h1 = gdisk.primary_header().unwrap();
    assert_eq!(h1.current_lba, 1);
    assert_eq!(h1.backup_lba, 95);

    let h2 = gdisk.backup_header().unwrap();
    assert_eq!(h2.current_lba, 95);
    assert_eq!(h2.backup_lba, 1);

    let p1 = &gdisk.partitions().get(&1_u32).unwrap();
    assert_eq!(p1.name, "primary");
    let p1_start = p1.bytes_start(*gdisk.logical_block_size()).unwrap();
    assert_eq!(p1_start, 0x22 * 512);
    let p1_len = p1.bytes_len(*gdisk.logical_block_size()).unwrap();
    assert_eq!(p1_len, (0x3E - 0x22 + 1) * 512);
}

#[test]
fn test_gptdisk_linux_01_write_fidelity_with_device() {
    let diskpath = path::Path::new("tests/fixtures/gpt-linux-disk-01.img");

    // Assumes that test_gptdisk_linux_01 has passed, no need to check answers.
    let mut gdisk = gpt::GptConfig::new().open(diskpath).unwrap();
    let good_header1 = gdisk.primary_header().unwrap().clone();
    let good_header2 = gdisk.backup_header().unwrap().clone();
    let good_partitions = gdisk.partitions().clone();
    println!("good header1={:?}", good_header1);
    println!("good header2={:?}", good_header2);
    println!("good partitions={:#?}", good_partitions);

    // Test that we can write this test partition table to an in-memory buffer
    // instead, then load the results and verify they should be the same.
    let image_size = usize::try_from(std::fs::metadata(diskpath).unwrap().len()).unwrap();
    let mem_device = Box::new(std::io::Cursor::new(vec![0_u8; image_size]));
    gdisk.update_disk_device(mem_device, true);
    let mut mem_device = gdisk.write().unwrap();

    // Write this memory buffer to a temp file, and load from the file to verify
    // that we wrote the data to the memory buffer correctly.
    let mut tempdisk = NamedTempFile::new().expect("failed to create tempfile disk");
    let mut gpt_in_mem = vec![0_u8; image_size];
    let _ = mem_device.seek(SeekFrom::Start(0)).unwrap();
    mem_device.read_exact(&mut gpt_in_mem).unwrap();
    tempdisk.write_all(&gpt_in_mem).unwrap();
    tempdisk.flush().unwrap();

    let gdisk_file = gpt::GptConfig::new().open(tempdisk.path()).unwrap();
    println!("file header1={:?}", gdisk_file.primary_header().unwrap());
    println!("file header2={:?}", gdisk_file.backup_header().unwrap());
    println!("file partitions={:#?}", gdisk_file.partitions());
    assert_eq!(gdisk_file.primary_header().unwrap(), &good_header1);
    assert_eq!(gdisk_file.backup_header().unwrap(), &good_header2);
    assert_eq!(gdisk_file.partitions().clone(), good_partitions);

    // Test that if we read it back from this memory buffer, it matches the known good.
    let gdisk_mem = gpt::GptConfig::new().open_from_device(mem_device).unwrap();
    assert_eq!(gdisk_mem.primary_header().unwrap(), &good_header1);
    assert_eq!(gdisk_mem.backup_header().unwrap(), &good_header2);
    assert_eq!(gdisk_mem.partitions().clone(), good_partitions);
}

#[test]
fn test_create_simple_on_device() {
    const TOTAL_BYTES: usize = 1024 * 64;
    let mut mem_device = Box::new(std::io::Cursor::new(vec![0_u8; TOTAL_BYTES]));

    // Create a protective MBR at LBA0
    let mbr = gpt::mbr::ProtectiveMBR::with_lb_size(
        u32::try_from((TOTAL_BYTES / 512) - 1).unwrap_or(0xFF_FF_FF_FF));
    mbr.overwrite_lba0(&mut mem_device).unwrap();

    let mut gdisk = gpt::GptConfig::default()
        .initialized(false)
        .writable(true)
        .logical_block_size(disk::LogicalBlockSize::Lb512)
        .create_from_device(mem_device, None)
        .unwrap();
    // Initialize the headers using a blank partition table
    gdisk.update_partitions(BTreeMap::<u32, gpt::partition::Partition>::new()).unwrap();
    // At this point, gdisk.primary_header() and gdisk.backup_header() are populated...
    // Add a few partitions to demonstrate how...
    gdisk.add_partition("test1", 1024 * 12, gpt::partition_types::BASIC, 0, None).unwrap();
    gdisk.add_partition("test2", 1024 * 18, gpt::partition_types::LINUX_FS, 0, None).unwrap();
    let mut mem_device = gdisk.write().unwrap();
    mem_device.seek(std::io::SeekFrom::Start(0)).unwrap();
    let mut final_bytes = vec![0_u8; TOTAL_BYTES];
    mem_device.read_exact(&mut final_bytes).unwrap();
}

fn test_create_aligned_on_device() {
    const TOTAL_BYTES: usize = 48 * 1024; // 48KiB, 96 Blocks
    const ALIGNMENT: u64 = 4096 / 512; // 8 LBA alignment

    let mut mem_device = Box::new(std::io::Cursor::new(vec![0u8; TOTAL_BYTES]));

    let mbr = gpt::mbr::ProtectiveMBR::with_lb_size(u32::try_from((TOTAL_BYTES / 512) - 1).unwrap_or(0xFF_FF_FF_FF));
    mbr.overwrite_lba0(&mut mem_device).unwrap();

    let mut gdisk = gpt::GptConfig::default()
        .initialized(false)
        .writable(true)
        .logical_block_size(disk::LogicalBlockSize::Lb512)
        .create_from_device(mem_device, None)
        .unwrap();
    gdisk.update_partitions(BTreeMap::<u32, gpt::partition::Partition>::new()).unwrap();

    // 00-33: MBR, GPT Header / Info
    // 40-51: Part 1 - 0.75 disk pages
    // 56-61: Part 2 - 0.75 disk pagess
    // 62-95: GPT Backup
    assert!(gdisk.add_partition("test1", 6 * 1024, gpt::partition_types::BASIC, 0, Some(ALIGNMENT)).is_ok(),
            "unexpected error writing first aligned partition: should start at LBA 40, end at 59");

    assert!(gdisk.add_partition("test2", 8 * 1024, gpt::partition_types::LINUX_FS, 0, Some(ALIGNMENT)).is_err(),
            "expected error writing over-sized second aligned partition: impossible addressing starting at LBA 56 ending at LBA 63 shouldn't fit with GPT backup");

    assert!(gdisk.add_partition("test2", 6 * 1024, gpt::partition_types::LINUX_FS, 0, Some(ALIGNMENT)).is_ok(),
            "unexpected error writing second aligned partition: should start at LBA 56, end at 61");

    let mut mem_device = gdisk.write().unwrap();
    let mut final_bytes = vec![0u8; TOTAL_BYTES];
    mem_device.read_exact(&mut final_bytes).unwrap();

}

fn t_read_bytes(device: &mut gpt::DiskDeviceObject, offset: u64, bytes: usize) -> Vec<u8> {
    let mut buf = vec![0_u8; bytes];
    device.seek(std::io::SeekFrom::Start(offset)).unwrap();
    device.read_exact(&mut buf).unwrap();
    buf
}

fn test_helper_gptdisk_write_efi_unused_partition_entries(lb_size: disk::LogicalBlockSize) {
    // Test that we write zeros to unused areas of the partition array, so that
    // if we're creating a partition table from scratch (not loading an existing
    // table and modifying it) it will create a partition array that is UEFI
    // compliant (has 128 entries) and unused entries are properly initialized
    // with zeros.

    let lb_bytes: u64 = lb_size.into();
    let lb_bytes_usize = lb_bytes as usize;
    // protective MBR + GPT header + GPT partition array
    let header_lbs = 1 + 1 + ((128 * 128) / lb_bytes);
    assert_eq!((128 * 128) % lb_bytes, 0);
    let data_lbs = 10;
    // GPT partition array + GPT header
    let footer_lbs = ((128 * 128) / lb_bytes) + 1;
    let total_lbs = header_lbs + data_lbs + footer_lbs;
    let total_bytes = (total_lbs * lb_bytes) as usize;

    // Initialize the buffer with all '255' values so we can tell what's been overwritten vs preserved.
    let mem_device = Box::new(std::io::Cursor::new(vec![255u8; total_bytes]));

    // Setup a new partition table and add a couple entries to it.
    let mut gdisk = gpt::GptConfig::default()
        .initialized(false)
        .writable(true)
        .logical_block_size(lb_size)
        .create_from_device(mem_device, None)
        .unwrap();
    // Initialize the headers using a blank partition table.
    gdisk.update_partitions(BTreeMap::<u32, gpt::partition::Partition>::new()).unwrap();

    let part1_bytes = 3 * lb_bytes;
    gdisk.add_partition("test1", part1_bytes, gpt::partition_types::BASIC, 0, None).unwrap();
    gdisk.add_partition("test2", (data_lbs * lb_bytes) - part1_bytes, gpt::partition_types::LINUX_FS, 0, None).unwrap();

    // Write out the table and get back the memory buffer so we can validate its contents.
    let mut mem_device = gdisk.write().unwrap();
    // Should NOT have overwritten the MBR (we have to generate a protective MBR explicitly using mbr module)
    assert_eq!(t_read_bytes(&mut mem_device, 0, lb_bytes_usize), vec![255u8; lb_bytes_usize]);
    // Should have overwritten the header
    assert_ne!(t_read_bytes(&mut mem_device, lb_bytes, 92), vec![255u8; 92]);
    // According to the spec, the rest of the sector containing the header should be zeros.
    assert_eq!(t_read_bytes(&mut mem_device, lb_bytes + 92, lb_bytes_usize - 92), vec![0_u8; lb_bytes_usize - 92]);
    // The first two partition entries should have been overwritten with non-zero data.
    let first_two = t_read_bytes(&mut mem_device, 2 * lb_bytes, 128 * 2);
    assert_ne!(first_two, vec![255u8; 128 * 2]);
    assert_ne!(first_two, vec![0_u8; 128 * 2]);
    // The remaining entries should have been overwritten with all zeros.
    assert_eq!(t_read_bytes(&mut mem_device, (2 * lb_bytes) + (128 * 2), 126 * 128), vec![0_u8; 126 * 128]);

    // The data area should be completely undisturbed...
    let data_bytes = (data_lbs as usize) * lb_bytes_usize;
    assert_eq!(t_read_bytes(&mut mem_device, header_lbs * lb_bytes, data_bytes), vec![255u8; data_bytes]);

    // The first two partition entries in the volume footer should have been overwritten with non-zero data.
    // The remaining entries should have been overwritten with all zeros.
    let first_two = t_read_bytes(&mut mem_device, (header_lbs + data_lbs) * lb_bytes, 128 * 2);
    assert_ne!(first_two, vec![255u8; 128 * 2]);
    assert_ne!(first_two, vec![0_u8; 128 * 2]);
    // The remaining entries should have been overwritten with all zeros.
    assert_eq!(t_read_bytes(&mut mem_device, (2 * lb_bytes) + (128 * 2), 126 * 128), vec![0_u8; 126 * 128]);

    // Should have overwritten the backup header
    assert_ne!(t_read_bytes(&mut mem_device, total_bytes as u64 - lb_bytes, 92), vec![255u8; 92]);
    // Remainder of the sector with the backup header should be all zeros
    assert_eq!(t_read_bytes(&mut mem_device, total_bytes as u64 - lb_bytes + 92, lb_bytes_usize - 92), vec![0_u8; lb_bytes_usize - 92]);
}

#[test]
fn test_gptdisk_write_efi_unused_partition_entries_512() {
    test_helper_gptdisk_write_efi_unused_partition_entries(disk::LogicalBlockSize::Lb512);
}

#[test]
fn test_gptdisk_write_efi_unused_partition_entries_4096() {
    test_helper_gptdisk_write_efi_unused_partition_entries(disk::LogicalBlockSize::Lb4096);
}
