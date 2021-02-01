use {
    anyhow::Error,
    remote_block_device::Cache,
    crate::{
        device::Device,
        object_store::{self, Filesystem, filesystem::SyncOptions},
    },
    std::sync::{Arc, Mutex},
};

pub fn mkfs(cache: Cache) -> Result<(), Error> {
    let mut fs = Filesystem::new_empty(Arc::new(Device{cache: Mutex::new(cache)}))?;
    fs.sync(SyncOptions::default())?;
    Ok(())
}
