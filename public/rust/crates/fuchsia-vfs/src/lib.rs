// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia VFS Server Bindings

#![deny(warnings)]

extern crate bytes;
extern crate fdio;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
#[macro_use]
extern crate futures;
extern crate libc;

use std::path::Path;
use std::sync::Arc;
use zx::AsHandleRef;

mod mount;

pub mod vfs;
pub use vfs::*;

pub fn mount(
    path: &Path,
    vfs: Arc<Vfs>,
    vn: Arc<Vnode>,
) -> Result<mount::Mount, zx::Status> {
    let (c1, c2) = zx::Channel::create()?;
    let m = mount::mount(path, c1)?;
    c2.signal_handle(
        zx::Signals::NONE,
        zx::Signals::USER_0,
    )?;
    let c = Connection::new(Arc::clone(&vfs), vn, c2)?;
    vfs.register_connection(c);
    Ok(m)
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::channel::oneshot;
    use futures::io;
    use std::fs;
    use std::thread;

    extern crate tempdir;

    struct BasicFS {}

    impl Vfs for BasicFS {}

    struct BasicVnode {}

    impl Vnode for BasicVnode {}

    #[test]
    fn mount_basic() {
        let mut executor = async::Executor::new().unwrap();

        let bfs = Arc::new(BasicFS {});
        let bvn = Arc::new(BasicVnode {});

        let d = tempdir::TempDir::new("mount_basic").unwrap();

        let m = mount(&d.path(), bfs, bvn).expect("mount");

        let (tx, rx) = oneshot::channel::<io::Error>();

        let path = d.path().to_owned();

        thread::spawn(move || {
            let e = fs::OpenOptions::new()
                .read(true)
                .open(path)
                .expect_err("expected notsupported");
            tx.send(e).unwrap();
        });

        let e = executor.run_singlethreaded(rx).unwrap();

        assert_eq!(io::ErrorKind::Other, e.kind());

        std::mem::drop(m);
    }
}
