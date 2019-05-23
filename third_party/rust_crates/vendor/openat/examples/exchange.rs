extern crate argparse;
extern crate openat;

use std::process::exit;
use std::path::PathBuf;

use argparse::{ArgumentParser, Parse};
use openat::Dir;

#[cfg(not(target_os="linux"))]
fn main() {
    println!("Atomic exchange is not supported on this platform")
}

#[cfg(target_os="linux")]
fn main() {
    let mut path1 = PathBuf::new();
    let mut path2 = PathBuf::new();
    {
        let mut ap = ArgumentParser::new();
        ap.refer(&mut path1)
            .add_argument("path1", Parse, "First path of exchange operation")
            .required();
        ap.refer(&mut path2)
            .add_argument("path2", Parse, "Second path of exchange operation")
            .required();
        ap.parse_args_or_exit();
    }
    if path1.parent() != path2.parent() {
        println!("Paths must be in the same directory");
        exit(1);
    }
    let parent = path1.parent().expect("path must have parent directory");
    let dir = Dir::open(parent).expect("can open directory");
    dir.local_exchange(
        path1.file_name().expect("path1 must have filename"),
        path2.file_name().expect("path2 must have filename"),
    ).expect("can rename");
}
