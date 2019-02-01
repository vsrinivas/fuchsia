use {
    failure::Error,
    fuchsia_syslog::fx_log_warn,
    fuchsia_vfs_watcher::{self as vfs_watcher, WatchEvent, WatchMessage},
    futures::{Stream, TryStreamExt},
    std::{
        fs::File,
        io,
        path::{Path, PathBuf}
    },
};

// This module defines a watcher that subscribes to the device filesystem and
// produces a stream of messages when bt-host devices are added or removed from
// the system

static BT_HOST_DIR: &str = "/dev/class/bt-host";

fn bt_host_path() -> &'static Path { Path::new(BT_HOST_DIR) }

pub enum AdapterEvent {
    AdapterAdded(PathBuf),
    AdapterRemoved(PathBuf)
}

use self::AdapterEvent::*;

/// Watch the VFS for host adapter devices being added or removed, and produce
/// a stream of AdapterEvent messages
pub fn watch_hosts() -> impl Stream<Item = Result<AdapterEvent, Error>> {
    let dev = File::open(&BT_HOST_DIR);
    let watcher = vfs_watcher::Watcher::new(&dev.unwrap())
        .expect("Cannot open vfs watcher for bt-host device path");
    watcher.try_filter_map(as_adapter_msg).map_err(|e| e.into())
}

pub async fn as_adapter_msg(msg: WatchMessage) -> Result<Option<AdapterEvent>, io::Error> {
    let path = bt_host_path().join(&msg.filename);
    Ok(match msg.event {
        WatchEvent::EXISTING | WatchEvent::ADD_FILE => Some(AdapterAdded(path)),
        WatchEvent::REMOVE_FILE => Some(AdapterRemoved(path)),
        WatchEvent::IDLE => None,
        e => {
            fx_log_warn!("Unrecognized host watch event: {:?}", e);
            None
        },
    })
}

