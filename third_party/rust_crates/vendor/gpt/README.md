# gpt

[![Build Status](https://travis-ci.org/Quyzi/gpt.svg?branch=master)](https://travis-ci.org/Quyzi/gpt)
[![crates.io](https://img.shields.io/crates/v/gpt.svg)](https://crates.io/crates/gpt)
![minimum rust 1.34](https://img.shields.io/badge/rust-1.34%2B-orange.svg)
[![Documentation](https://docs.rs/gpt/badge.svg)](https://docs.rs/gpt)

A pure-Rust library to work with GPT partition tables.

`gpt` provides support for manipulating (R/W) GPT headers and partition
tables. It supports raw disk devices as well as disk images.

## Example

```rust
extern crate gpt;
use gpt::header::{Header, read_header};
use gpt::partition::{Partition, read_partitions};

fn inspect_disk() {
    let lb_size = gpt::disk::DEFAULT_SECTOR_SIZE;
    let diskpath = std::path::Path::new("/dev/sdz");

    let hdr = header::read_header(diskpath, lb_size).unwrap();
    println!("Disk header: {:#?}", h);

    let pp = read_partitions(diskpath, &hdr, lb_size).unwrap();
    println!("Partition layout: {:#?}", pp);
}
```
