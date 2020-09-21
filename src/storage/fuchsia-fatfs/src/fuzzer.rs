// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        directory::FatDirectory,
        node::{Closer, FatNode, Node},
        FatFs,
    },
    anyhow::Error,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_zircon::Status,
    futures::{future::BoxFuture, prelude::*},
    scopeguard::defer,
    std::{any::Any, io::Cursor, sync::Arc},
    vfs::{
        directory::{
            dirents_sink::{AppendResult, Sealed, Sink},
            entry::EntryInfo,
            entry_container::Directory,
            traversal_position::TraversalPosition,
        },
        file::File,
    },
};

fn fuzz_node(fs: &FatFs, node: FatNode, depth: u32) -> BoxFuture<'_, Result<(), Status>> {
    async move {
        if depth > 5 {
            // It's possible for a FAT filesystem to contain cycles while technically being legal.
            return Ok(());
        }
        match node {
            FatNode::File(file) => {
                let _ = file.read_at(0, 2048).await;
                let _ = file.write_at(256, "qwerty".as_bytes()).await;
                let _ = file.get_size().await;
            }
            FatNode::Dir(dir) => {
                let sink = FuzzSink::new(dir.clone(), depth);
                let (pos, sealed): (TraversalPosition, Box<dyn Sealed>) =
                    dir.read_dirents(&TraversalPosition::Start, Box::new(sink)).await?;
                assert_eq!(pos, TraversalPosition::End);
                let sink = sealed.open().downcast::<FuzzSink>().unwrap();
                sink.walk(fs).await;
            }
        };

        Ok(())
    }
    .boxed()
}

struct FuzzSink {
    entries: Vec<String>,
    dir: Arc<FatDirectory>,
    depth: u32,
}

impl FuzzSink {
    fn new(dir: Arc<FatDirectory>, depth: u32) -> Self {
        Self { entries: Vec::new(), dir, depth }
    }

    async fn walk(&self, fs: &FatFs) {
        for name in self.entries.iter() {
            let mut closer = Closer::new(fs.filesystem());
            let entry = match self.dir.open_child(
                name,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                0,
                &mut closer,
            ) {
                Ok(entry) => entry,
                Err(_) => continue,
            };

            let _ = fuzz_node(fs, entry, self.depth + 1).await;
        }
    }
}

impl Sink for FuzzSink {
    fn append(mut self: Box<Self>, _entry: &EntryInfo, name: &str) -> AppendResult {
        if name != ".." && name != "." {
            self.entries.push(name.to_owned());
        }
        AppendResult::Ok(self)
    }

    fn seal(self: Box<Self>) -> Box<dyn Sealed> {
        self
    }
}

impl Sealed for FuzzSink {
    fn open(self: Box<Self>) -> Box<dyn Any> {
        self
    }
}

async fn do_fuzz(disk: Cursor<Vec<u8>>) -> Result<(), Error> {
    let fs = FatFs::new(Box::new(disk))?;
    let root: Arc<FatDirectory> = fs.get_fatfs_root();

    root.open_ref(&fs.filesystem().lock().unwrap()).unwrap();
    let _ = fuzz_node(&fs, FatNode::Dir(root.clone()), 0).await;
    defer! { root.close().unwrap() };

    Ok(())
}

pub fn fuzz_fatfs(fs: &[u8]) {
    let mut executor = fuchsia_async::Executor::new().unwrap();
    executor.run_singlethreaded(async {
        let mut vec = fs.to_vec();
        // Make sure the "disk" is always a length that's a multiple of 512.
        let rounded = ((vec.len() / 512) + 1) * 512;
        vec.resize(rounded, 0);
        let cursor = std::io::Cursor::new(vec);

        let _ = do_fuzz(cursor).await;
    });
}
