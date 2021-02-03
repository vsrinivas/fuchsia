use {
    crate::{
        device::Device,
        object_store::{filesystem::SyncOptions, Filesystem},
    },
    anyhow::Error,
    remote_block_device::Cache,
    std::sync::Arc,
};

pub fn mkfs(cache: Cache) -> Result<(), Error> {
    let mut fs = Filesystem::new_empty(Arc::new(Device::new(cache)))?;
    fs.sync(SyncOptions::default())?;
    Ok(())
}
