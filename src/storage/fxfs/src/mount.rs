use {
    anyhow::Error,
    crate::{
        device::Device,
        object_store::{self, Filesystem, filesystem::SyncOptions},
    },
};

pub fn mount(cache: Cache) -> Result<(), Error> {
    let device = Device{cache: Mutex::new(cache)};
    let _filesystem = Filesystem::open(device)?;
}
