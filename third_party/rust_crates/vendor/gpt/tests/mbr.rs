
use tempfile;

use gpt::{disk, mbr};
use std::fs::File;
use std::io::Read;

#[test]
fn test_mbr_partrecord() {
    let pr0 = mbr::PartRecord::zero();
    let data0 = pr0.as_bytes().unwrap();
    assert_eq!(data0.len(), 16);
    assert_eq!(data0, [0x00; 16]);

    let pr1 = mbr::PartRecord::new_protective(None);
    let data1 = pr1.as_bytes().unwrap();
    assert_eq!(data0.len(), data1.len());
    assert_ne!(data0, data1);
}

#[test]
fn test_mbr_protective() {
    let m0 = mbr::ProtectiveMBR::new();
    let data0 = m0.as_bytes().unwrap();
    assert_eq!(data0.len(), 512);
    assert_eq!(data0[510], 0x55);
    assert_eq!(data0[511], 0xAA);

    let m1 = mbr::ProtectiveMBR::with_lb_size(0x01);
    let data1 = m1.as_bytes().unwrap();
    assert_eq!(data0.len(), data1.len());
    assert_ne!(data0, data1);
    assert_eq!(data1[510], 0x55);
    assert_eq!(data1[511], 0xAA);
}

#[test]
fn test_mbr_write() {
    let mut tempdisk = tempfile::tempfile().unwrap();
    let m0 = mbr::ProtectiveMBR::new();
    let data0 = m0.as_bytes().unwrap();
    m0.overwrite_lba0(&mut tempdisk).unwrap();
    m0.update_conservative(&mut tempdisk).unwrap();

    let mut buf = Vec::new();
    let size = tempdisk.read_to_end(&mut buf).unwrap();
    assert!(size != 0);
    assert_eq!(buf, data0);
}

#[test]
fn test_mbr_read() {
    let mut diskf = File::open("tests/fixtures/gpt-linux-disk-01.img").unwrap();
    let m0 = mbr::ProtectiveMBR::from_disk(&mut diskf, disk::LogicalBlockSize::Lb512).unwrap();
    assert_eq!(m0.bootcode().to_vec(), vec![0; 440]);
    assert_eq!(m0.disk_signature().to_vec(), vec![0; 4]);
}

#[test]
fn test_mbr_rw_roundtrip() {
    let mut tempdisk = tempfile::tempfile().unwrap();
    let mut m0 = mbr::ProtectiveMBR::new();
    let newsig = [0x11, 0x22, 0x33, 0x44];
    m0.set_disk_signature(newsig);
    m0.overwrite_lba0(&mut tempdisk).unwrap();
    let data0 = m0.as_bytes().unwrap();

    let m1 = mbr::ProtectiveMBR::from_disk(&mut tempdisk, disk::LogicalBlockSize::Lb512).unwrap();
    let data1 = m1.as_bytes().unwrap();
    assert_eq!(m0.bootcode().to_vec(), m1.bootcode().to_vec());
    assert_eq!(m0.disk_signature().to_vec(), m1.disk_signature().to_vec());
    assert_eq!(m0.disk_signature().to_vec(), newsig);
    assert_eq!(data0, data1);
}

#[test]
fn test_mbr_bootcode() {
    let mut tempdisk = tempfile::tempfile().unwrap();
    let m0 = mbr::ProtectiveMBR::new();
    m0.overwrite_lba0(&mut tempdisk).unwrap();

    let b0 = mbr::read_bootcode(&mut tempdisk).unwrap();
    assert_eq!(b0.len(), 440);
    assert_eq!(b0.to_vec(), vec![0; 440]);

    let b1 = [0xAA; 440];
    mbr::write_bootcode(&mut tempdisk, &b1).unwrap();
    let b2 = mbr::read_bootcode(&mut tempdisk).unwrap();
    assert_eq!(b1.to_vec(), b2.to_vec());
}

#[test]
fn test_mbr_disksig() {
    let mut tempdisk = tempfile::tempfile().unwrap();
    let m0 = mbr::ProtectiveMBR::new();
    m0.overwrite_lba0(&mut tempdisk).unwrap();

    let s0 = mbr::read_disk_signature(&mut tempdisk).unwrap();
    assert_eq!(s0.len(), 4);
    assert_eq!(s0.to_vec(), vec![0; 4]);

    let s1 = [0x00, 0x11, 0x22, 0x33];
    mbr::write_disk_signature(&mut tempdisk, &s1).unwrap();
    let s2 = mbr::read_disk_signature(&mut tempdisk).unwrap();
    assert_eq!(s1.to_vec(), s2.to_vec());
}
