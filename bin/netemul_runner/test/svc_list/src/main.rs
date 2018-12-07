// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs;
use std::io;
use std::path::Path;

fn visit_dirs(dir: &Path) -> io::Result<()> {
    if dir.is_dir() {
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            let str = path.to_str().expect("paths need strings?");
            println!("{}", str);
            visit_dirs(dir.join(path).as_path())?;
        }
    }
    Ok(())
}

fn main() -> io::Result<()> {
    println!("Hello World");
    visit_dirs(Path::new("/"))?;

    // TODO(brunodalbo) the idea of this test is to find the sandbox services and ensure they're there

    Ok(())
}
