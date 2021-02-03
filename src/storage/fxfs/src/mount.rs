use {
    crate::{device::Device, object_store::Filesystem},
    anyhow::Error,
    remote_block_device::Cache,
    std::sync::Arc,
};

pub fn mount(cache: Cache) -> Result<(), Error> {
    let device = Arc::new(Device::new(cache));
    let _filesystem = Filesystem::open(device)?;
    // TODO
    Ok(())
}
