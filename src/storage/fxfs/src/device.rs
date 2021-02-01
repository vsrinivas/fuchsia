use {
    anyhow::Error,
    remote_block_device::Cache,
    crate::object_store::{self, Filesystem, filesystem::SyncOptions},
    std::sync::{Arc, Mutex},
};

struct Device {
    cache: Mutex<Cache>
}

const BLOCK_SIZE: u64 = 8192;

fn map_to_io_error(error: anyhow::Error) -> std::io::Error {
    // TODO
    std::io::Error::new(std::io::ErrorKind::Other, format!("{}", error))
}

impl object_store::Device for Device {
    fn block_size(&self) -> u64 {
        return BLOCK_SIZE;
    }

    fn read(&self, offset: u64, buf: &mut [u8]) -> std::io::Result<()> {
        self.cache.lock().unwrap().read_at(buf, offset).map_err(map_to_io_error)
    }

    fn write(&self, offset: u64, buf: &[u8]) -> std::io::Result<()> {
        self.cache.lock().unwrap().write_at(buf, offset).map_err(map_to_io_error)
    }
}
