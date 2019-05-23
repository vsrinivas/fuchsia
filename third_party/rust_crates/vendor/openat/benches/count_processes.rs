#![feature(test)]

extern crate openat;
extern crate test;


use std::fs::read_dir;
use std::str::from_utf8;
use std::os::unix::ffi::OsStrExt;
use test::Bencher;

use openat::Dir;


#[bench]
fn procs_stdlib(b: &mut Bencher) {
    b.iter(|| {
        read_dir("/proc").unwrap().filter(|r| {
            r.as_ref().ok()
            .and_then(|e| from_utf8(e.file_name().as_bytes()).ok()
            // pid is everything that can be parsed as a number
                .and_then(|s| s.parse::<u32>().ok()))
            .is_some()
        }).count()
    });
}

#[bench]
fn procs_openat(b: &mut Bencher) {
    b.iter(|| {
        Dir::open("/proc").unwrap().list_dir(".").unwrap().filter(|r| {
            r.as_ref().ok()
            .and_then(|e| from_utf8(e.file_name().as_bytes()).ok()
            // pid is everything that can be parsed as a number
                .and_then(|s| s.parse::<u32>().ok()))
            .is_some()
        }).count()
    });
}
