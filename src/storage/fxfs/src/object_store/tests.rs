use {
    crate::{
        lsm_tree::LSMTree,
        object_handle::ObjectHandle,
        object_store,
        object_store::{
            filesystem::{Filesystem, SyncOptions},
            log::Transaction,
            merge,
            record::{ExtentKey, ObjectItem, ObjectKey, ObjectValue},
            HandleOptions,
        },
    },
    anyhow::Error,
    std::sync::{Arc, Mutex},
};

struct Device {
    block_size: u64,
    data: Mutex<Vec<u8>>,
}

impl Device {
    fn new(block_size: u64) -> Device {
        Device { block_size, data: Mutex::new(Vec::new()) }
    }
}

impl object_store::Device for Device {
    fn block_size(&self) -> u64 {
        self.block_size
    }
    fn read(&self, offset: u64, buf: &mut [u8]) -> std::io::Result<()> {
        assert!(offset % self.block_size == 0);
        assert!(buf.len() % self.block_size as usize == 0);
        let required_len = offset as usize + buf.len();
        let data = self.data.lock().unwrap();
        assert!(data.len() >= required_len);
        buf.copy_from_slice(&data[offset as usize..offset as usize + buf.len()]);
        Ok(())
    }

    fn write(&self, offset: u64, buf: &[u8]) -> std::io::Result<()> {
        assert!(buf.len() % self.block_size as usize == 0);
        let end = offset as usize + buf.len();
        let mut data = self.data.lock().unwrap();
        if data.len() < end {
            data.resize(end, 0);
        }
        data[offset as usize..end].copy_from_slice(buf);
        Ok(())
    }
}

#[test]
fn test_object_store() -> Result<(), Error> {
    let device = Arc::new(Device::new(512));
    let object_id;
    {
        let mut filesystem = Filesystem::new_empty(device.clone())?;
        let root_store = filesystem.root_store();
        let mut transaction = Transaction::new();
        let handle = root_store
            .create_object(&mut transaction, HandleOptions::default())
            .expect("create_object failed");
        root_store.log().commit(transaction);
        object_id = handle.object_id();
        handle.write(5, b"hello").expect("write failed");
        {
            let mut buf = [0; 5];
            handle.read(5, &mut buf).expect("read failed");
        }
        handle.write(6, b"hello").expect("write failed");
        {
            let mut buf = [0; 6];
            handle.read(5, &mut buf).expect("read failed");
        }
        filesystem.sync(SyncOptions::default()).expect("sync failed");
    }
    let object_id2;
    {
        let mut filesystem = Filesystem::open(device.clone()).expect("open failed");
        let root_store = filesystem.root_store();
        let handle = root_store
            .open_object(object_id, HandleOptions::default())
            .expect("open_object failed");
        let mut buf = [0; 5];
        handle.read(6, &mut buf).expect("read failed");
        assert_eq!(&buf, b"hello");
        let mut transaction = Transaction::new();
        let handle = root_store
            .create_object(&mut transaction, HandleOptions::default())
            .expect("create_object failed");
        root_store.log().commit(transaction);
        object_id2 = handle.object_id();
        handle.write(5000, b"foo").expect("write failed");
        filesystem
            .sync(SyncOptions { new_super_block: true, ..Default::default() })
            .expect("sync failed");
    }
    {
        let mut filesystem = Filesystem::open(device.clone()).expect("open failed");
        let root_store = filesystem.root_store();
        let handle = root_store
            .open_object(object_id2, HandleOptions::default())
            .expect("open_object failed");
        let mut buf = [0; 3];
        handle.read(5000, &mut buf).expect("read failed");
        assert_eq!(&buf, b"foo");
        root_store.flush(true).expect("flush failed");
        filesystem
            .sync(SyncOptions { new_super_block: true, ..Default::default() })
            .expect("sync failed");
    }
    let mut object_ids = Vec::new();
    {
        let filesystem = Filesystem::open(device.clone()).expect("open failed");
        let root_store = filesystem.root_store();
        for _i in 0..500 {
            let mut transaction = Transaction::new();
            let handle = root_store
                .create_object(&mut transaction, HandleOptions::default())
                .expect("create_object failed");
            root_store.log().commit(transaction);
            object_ids.push(handle.object_id());
            handle.write(10000, b"bar").expect("write failed");
        }
    }
    {
        let filesystem = Filesystem::open(device.clone()).expect("open failed");
        let root_store = filesystem.root_store();
        let handle = root_store
            .open_object(object_ids[0], HandleOptions::default())
            .expect("open_object failed");
        let mut buf = [0; 3];
        handle.read(10000, &mut buf).expect("read failed");
        assert_eq!(&buf, b"bar");
    }
    Ok(())
}

#[test]
fn test_extents_merging() -> Result<(), Error> {
    let tree = LSMTree::new(merge::merge);
    let item = ObjectItem {
        key: ObjectKey::extent(0, ExtentKey::new(0, 0..10)),
        value: ObjectValue::extent(0),
    };
    let lower_bound = item.key.lower_bound();
    tree.replace_range(item, &lower_bound);
    let item = ObjectItem {
        key: ObjectKey::extent(0, ExtentKey::new(0, 3..7)),
        value: ObjectValue::extent(0),
    };
    let lower_bound = item.key.lower_bound();
    tree.replace_range(item, &lower_bound);
    tree.dump_mutable_layer();
    {
        let mut iter = tree.iter();
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 0..3)));
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 3..7)));
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 7..10)));
        iter.advance()?;
        assert!(iter.get().is_none());
    }
    {
        let mut iter = tree.iter();
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 0..3)));
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 3..7)));
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 7..10)));
        iter.advance()?;
        assert!(iter.get().is_none());
    }
    let item = ObjectItem {
        key: ObjectKey::extent(0, ExtentKey::new(0, 2..9)),
        value: ObjectValue::extent(0),
    };
    let lower_bound = item.key.lower_bound();
    tree.replace_range(item, &lower_bound);
    {
        let mut iter = tree.iter();
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 0..2)));
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 2..9)));
        iter.advance()?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 9..10)));
        iter.advance()?;
        assert!(iter.get().is_none());
    }
    Ok(())
}

#[test]
fn test_directory() -> Result<(), Error> {
    let device = Arc::new(Device::new(512));
    let directory_id;
    {
        let mut filesystem = Filesystem::new_empty(device.clone())?;
        let root_store = filesystem.root_store();
        let mut directory = root_store.create_directory().expect("create_directory failed");
        directory.create_child_file("foo").expect("create_child_file failed");
        directory_id = directory.object_id();
        let directory = root_store.open_directory(directory_id).expect("open directory failed");
        directory.lookup("foo").expect("open foo failed");
        directory.lookup("bar").map(|_| ()).expect_err("open bar succeeded");
        filesystem.sync(SyncOptions::default()).expect("sync failed");
    }
    {
        let filesystem = Filesystem::open(device.clone())?;
        let root_store = filesystem.root_store();
        let directory = root_store.open_directory(directory_id).expect("open directory failed");
        directory.lookup("foo").expect("open foo failed");
        directory.lookup("bar").map(|_| ()).expect_err("open bar succeeded");
    }
    Ok(())
}
