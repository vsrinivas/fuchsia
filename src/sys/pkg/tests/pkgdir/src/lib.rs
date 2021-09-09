// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_test_fidl_pkg::{Backing, ConnectError, HarnessMarker},
    fuchsia_component::client::connect_to_protocol,
    std::fmt::Debug,
};

mod directory;
mod file;
mod node;

fn repeat_by_n(seed: char, n: usize) -> String {
    std::iter::repeat(seed).take(n).collect()
}

async fn dirs_to_test() -> impl Iterator<Item = PackageSource> {
    let proxy = connect_to_protocol::<HarnessMarker>().unwrap();
    let connect = |backing| {
        let proxy = Clone::clone(&proxy);
        async move {
            let (dir, server) = create_proxy::<DirectoryMarker>().unwrap();
            let () = proxy.connect_package(backing, server).await.unwrap().unwrap();
            PackageSource { dir, backing }
        }
    };
    // TODO(fxbug.dev/75481): include a pkgdir backed package as well
    IntoIterator::into_iter([connect(Backing::Pkgfs).await])
}

struct PackageSource {
    backing: Backing,
    dir: DirectoryProxy,
}
impl PackageSource {
    #[allow(dead_code)]
    fn is_pkgfs(&self) -> bool {
        self.backing == Backing::Pkgfs
    }

    #[allow(dead_code)]
    fn is_pkgdir(&self) -> bool {
        self.backing == Backing::Pkgdir
    }
}

// TODO(fxbug.dev/75481): support pkgdir-backed packages and delete this test.
#[fuchsia::test]
async fn unsupported_backing() {
    let harness = connect_to_protocol::<HarnessMarker>().unwrap();
    let (_dir, server) = create_proxy::<DirectoryMarker>().unwrap();

    let res = harness.connect_package(Backing::Pkgdir, server).await.unwrap();

    assert_eq!(res, Err(ConnectError::UnsupportedBacking));
}

macro_rules! flag_list {
    [$($flag:ident),* $(,)?] => {
        [
            $((fidl_fuchsia_io::$flag, stringify!($flag))),*
        ]
    };
}

// flags in same order as the appear in io.fidl in an attempt to make it easier
// to keep this list up to date. Although if this list gets out of date it's
// not the end of the world, the debug printer just won't know how to decode
// them and will hex format the not-decoded flags.
const OPEN_FLAGS: &[(u32, &'static str)] = &flag_list![
    OPEN_RIGHT_ADMIN,
    OPEN_RIGHT_EXECUTABLE,
    OPEN_RIGHT_READABLE,
    OPEN_RIGHT_WRITABLE,
    OPEN_FLAG_CREATE,
    OPEN_FLAG_CREATE_IF_ABSENT,
    OPEN_FLAG_TRUNCATE,
    OPEN_FLAG_DIRECTORY,
    OPEN_FLAG_APPEND,
    OPEN_FLAG_NO_REMOTE,
    OPEN_FLAG_NODE_REFERENCE,
    OPEN_FLAG_DESCRIBE,
    OPEN_FLAG_POSIX,
    OPEN_FLAG_POSIX_WRITABLE,
    OPEN_FLAG_POSIX_EXECUTABLE,
    OPEN_FLAG_NOT_DIRECTORY,
    CLONE_FLAG_SAME_RIGHTS,
];

struct OpenFlags(u32);

impl Debug for OpenFlags {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut flags = self.0;
        let flag_strings = OPEN_FLAGS.into_iter().filter_map(|&flag| {
            if self.0 & flag.0 == flag.0 {
                flags &= !flag.0;
                Some(flag.1)
            } else {
                None
            }
        });
        let mut first = true;
        for flag in flag_strings {
            if !first {
                write!(f, " | ")?;
            }
            first = false;
            write!(f, "{}", flag)?;
        }
        if flags != 0 {
            if !first {
                write!(f, " | ")?;
            }
            first = false;
            write!(f, "{:#x}", flags)?;
        }
        if first {
            write!(f, "0")?;
        }

        Ok(())
    }
}
