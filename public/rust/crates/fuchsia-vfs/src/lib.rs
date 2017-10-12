// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia VFS Server Bindings

extern crate bytes;
extern crate fdio;
extern crate fuchsia_zircon as zircon;
extern crate fuchsia_zircon_sys as zircon_sys;
extern crate futures;
extern crate libc;
#[macro_use]
extern crate tokio_core;
extern crate tokio_fuchsia;

use std::path::Path;
use std::sync::Arc;
use zircon::AsHandleRef;

mod mount;
#[macro_use]
#[allow(dead_code)]
mod remoteio;

pub mod vfs;
pub use vfs::*;

pub fn mount(
    path: &Path,
    vfs: Arc<Vfs>,
    vn: Arc<Vnode>,
    handle: &tokio_core::reactor::Handle,
) -> Result<mount::Mount, zircon::Status> {
    let (c1, c2) = zircon::Channel::create(zircon::ChannelOpts::default())?;
    let m = mount::mount(path, c1)?;
    c2.signal_handle(zircon::ZX_SIGNAL_NONE, zircon::ZX_USER_SIGNAL_0)?;
    let c = Connection::new(Arc::clone(&vfs), vn, c2, handle)?;
    vfs.register_connection(c, handle);
    Ok(m)
}

#[cfg(test)]
mod test {
    use super::*;

    extern crate tempdir;

    struct BasicFS {}

    impl Vfs for BasicFS {}

    struct BasicVnode {}

    impl Vnode for BasicVnode {}

    #[test]
    fn mount_basic() {
        let mut core = tokio_core::reactor::Core::new().unwrap();

        let bfs = Arc::new(BasicFS {});
        let bvn = Arc::new(BasicVnode {});

        let d = tempdir::TempDir::new("mount_basic").unwrap();

        let m = mount(&d.path(), bfs, bvn, &core.handle()).expect("mount");

        let (tx, rx) = futures::sync::oneshot::channel::<std::io::Error>();

        let path = d.path().to_owned();

        std::thread::spawn(move || {
            let e = std::fs::OpenOptions::new()
                .read(true)
                .open(path)
                .expect_err("expected notsupported");
            tx.send(e).unwrap();
        });

        let e = core.run(rx).unwrap();

        assert_eq!(std::io::ErrorKind::Other, e.kind());

        std::mem::drop(m);
    }
}
