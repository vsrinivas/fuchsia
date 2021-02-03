use {crate::object_store, remote_block_device::Cache, std::sync::Mutex};

pub struct Device {
    cache: Mutex<Cache>,
}

impl Device {
    pub fn new(cache: Cache) -> Self {
        Device { cache: Mutex::new(cache) }
    }
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
