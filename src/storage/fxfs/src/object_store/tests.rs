use {
    crate::{
        lsm_tree::LSMTree,
        object_handle::ObjectHandle,
        object_store::{
            filesystem::{Filesystem, FxFilesystem, SyncOptions},
            merge,
            record::{ExtentKey, ObjectItem, ObjectKey, ObjectValue},
            transaction::Transaction,
            HandleOptions,
        },
        testing::fake_device::FakeDevice,
    },
    anyhow::Error,
    fuchsia_async as fasync,
    std::sync::Arc,
};

#[fasync::run_singlethreaded(test)]
async fn test_object_store() -> Result<(), Error> {
    let device = Arc::new(FakeDevice::new(512));
    let object_id;
    {
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_store = filesystem.root_store();
        let mut transaction = Transaction::new();
        let handle = root_store
            .create_object(&mut transaction, HandleOptions::default())
            .await
            .expect("create_object failed");
        filesystem.commit_transaction(transaction).await;
        object_id = handle.object_id();
        handle.write(5, b"hello").await.expect("write failed");
        {
            let mut buf = [0; 5];
            handle.read(5, &mut buf).await.expect("read failed");
        }
        handle.write(6, b"hello").await.expect("write failed");
        {
            let mut buf = [0; 6];
            handle.read(5, &mut buf).await.expect("read failed");
        }
        filesystem.sync(SyncOptions::default()).await.expect("sync failed");
    }
    let object_id2;
    {
        let filesystem = FxFilesystem::open(device.clone()).await.expect("open failed");
        let root_store = filesystem.root_store();
        let handle = root_store
            .open_object(object_id, HandleOptions::default())
            .await
            .expect("open_object failed");
        let mut buf = [0; 5];
        handle.read(6, &mut buf).await.expect("read failed");
        assert_eq!(&buf, b"hello");
        let mut transaction = Transaction::new();
        let handle = root_store
            .create_object(&mut transaction, HandleOptions::default())
            .await
            .expect("create_object failed");
        filesystem.commit_transaction(transaction).await;
        object_id2 = handle.object_id();
        handle.write(5000, b"foo").await.expect("write failed");
        filesystem
            .sync(SyncOptions { new_super_block: true, ..Default::default() })
            .await
            .expect("sync failed");
    }
    {
        let filesystem = FxFilesystem::open(device.clone()).await.expect("open failed");
        let root_store = filesystem.root_store();
        let handle = root_store
            .open_object(object_id2, HandleOptions::default())
            .await
            .expect("open_object failed");
        let mut buf = [0; 3];
        handle.read(5000, &mut buf).await.expect("read failed");
        assert_eq!(&buf, b"foo");
        root_store.flush(true).await.expect("flush failed");
        filesystem
            .sync(SyncOptions { new_super_block: true, ..Default::default() })
            .await
            .expect("sync failed");
    }
    let mut object_ids = Vec::new();
    {
        let filesystem = FxFilesystem::open(device.clone()).await.expect("open failed");
        let root_store = filesystem.root_store();
        for _i in 0u16..500u16 {
            let mut transaction = Transaction::new();
            let handle = root_store
                .create_object(&mut transaction, HandleOptions::default())
                .await
                .expect("create_object failed");
            filesystem.commit_transaction(transaction).await;
            object_ids.push(handle.object_id());
            handle.write(10000, b"bar").await.expect("write failed");
        }
    }
    {
        let filesystem = FxFilesystem::open(device.clone()).await.expect("open failed");
        let root_store = filesystem.root_store();
        let handle = root_store
            .open_object(object_ids[0], HandleOptions::default())
            .await
            .expect("open_object failed");
        let mut buf = [0; 3];
        handle.read(10000, &mut buf).await.expect("read failed");
        assert_eq!(&buf, b"bar");
    }
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_extents_merging() -> Result<(), Error> {
    let tree = LSMTree::new(merge::merge);
    let item = ObjectItem {
        key: ObjectKey::extent(0, ExtentKey::new(0, 0..10)),
        value: ObjectValue::extent(0),
    };
    let lower_bound = item.key.search_key();
    tree.merge_into(item, &lower_bound).await;
    let item = ObjectItem {
        key: ObjectKey::extent(0, ExtentKey::new(0, 3..7)),
        value: ObjectValue::extent(0),
    };
    let lower_bound = item.key.search_key();
    tree.merge_into(item, &lower_bound).await;
    {
        let layer_set = tree.layer_set();
        let mut iter = layer_set.get_iterator();
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 0..3)));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 3..7)));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 7..10)));
        iter.advance().await?;
        assert!(iter.get().is_none());
    }
    {
        let layer_set = tree.layer_set();
        let mut iter = layer_set.get_iterator();
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 0..3)));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 3..7)));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 7..10)));
        iter.advance().await?;
        assert!(iter.get().is_none());
    }
    let item = ObjectItem {
        key: ObjectKey::extent(0, ExtentKey::new(0, 2..9)),
        value: ObjectValue::extent(0),
    };
    let lower_bound = item.key.search_key();
    tree.merge_into(item, &lower_bound).await;
    {
        let layer_set = tree.layer_set();
        let mut iter = layer_set.get_iterator();
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 0..2)));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 2..9)));
        iter.advance().await?;
        assert_eq!(iter.get().unwrap().key, &ObjectKey::extent(0, ExtentKey::new(0, 9..10)));
        iter.advance().await?;
        assert!(iter.get().is_none());
    }
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_directory() -> Result<(), Error> {
    let device = Arc::new(FakeDevice::new(512));
    let directory_id;
    {
        let filesystem = FxFilesystem::new_empty(device.clone()).await.expect("new_empty failed");
        let root_store = filesystem.root_store();
        let directory = root_store.create_directory().await.expect("create_directory failed");
        directory.create_child_file("foo").await.expect("create_child_file failed");
        directory_id = directory.object_id();
        let directory =
            root_store.open_directory(directory_id).await.expect("open directory failed");
        directory.lookup("foo").await.expect("open foo failed");
        directory.lookup("bar").await.map(|_| ()).expect_err("open bar succeeded");
        filesystem.sync(SyncOptions::default()).await.expect("sync failed");
    }
    {
        let filesystem = FxFilesystem::open(device.clone()).await.expect("open failed");
        let root_store = filesystem.root_store();
        let directory =
            root_store.open_directory(directory_id).await.expect("open directory failed");
        directory.lookup("foo").await.expect("open foo failed");
        directory.lookup("bar").await.map(|_| ()).expect_err("open bar succeeded");
    }
    Ok(())
}
