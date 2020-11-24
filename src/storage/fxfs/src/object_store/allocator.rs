mod merge;

#[cfg(test)]
mod tests;

use {
    crate::{
        lsm_tree::{
            skip_list_layer::SkipListLayer, Item, ItemRef, LSMTree, MutableLayer, OrdLowerBound,
        },
        object_handle::{ObjectHandle, ObjectHandleCursor},
        object_store::{
            log::{Log, Mutation, Transaction},
            HandleOptions, ObjectStore,
        },
    },
    anyhow::Error,
    bincode::{deserialize_from, serialize_into},
    merge::merge,
    serde::{Deserialize, Serialize},
    std::{
        borrow::Borrow,
        cmp::min,
        sync::{
            atomic::{AtomicU64, Ordering::Relaxed},
            Arc, Mutex,
        },
    },
};

pub type AllocatorItem = Item<AllocatorKey, AllocatorValue>;

/*
pub struct AllocatorReservation<'allocator> {
    allocator: &'allocator dyn Allocator,
    item: AllocatorItem,
}
*/

pub trait Allocator: Send + Sync {
    fn init(&self, store: Arc<ObjectStore>, handle: Box<dyn ObjectHandle>);

    fn object_id(&self) -> u64;

    // Returns device ranges.
    fn allocate(
        &self,
        object_id: u64,
        attribute_id: u64,
        object_range: std::ops::Range<u64>,
    ) -> Result<std::ops::Range<u64>, Error>;

    fn deallocate(
        &self,
        transaction: &mut Transaction,
        object_id: u64,
        attribute_id: u64,
        device_range: std::ops::Range<u64>,
        file_offset: u64,
    );

    // Called by log.
    fn set_next_block(&self, block: u64);

    fn flush(&self, force: bool) -> Result<(), Error>;

    fn commit_allocation(
        &self,
        object_id: u64,
        attribute_id: u64,
        device_range: std::ops::Range<u64>,
        file_offset: u64,
    );

    fn commit_deallocation(&self, item: AllocatorItem);

    fn open(&self, store: Arc<ObjectStore>, handle: Box<dyn ObjectHandle>) -> Result<(), Error>;
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct AllocatorKey {
    device_range: std::ops::Range<u64>,
    object_id: u64,
    attribute_id: u64,
    file_offset: u64,
}

impl AllocatorKey {
    fn lower_bound(&self) -> AllocatorKey {
        AllocatorKey {
            device_range: 0..self.device_range.start + 1,
            object_id: self.object_id,
            attribute_id: self.attribute_id,
            file_offset: self.file_offset,
        }
    }
}

impl Ord for AllocatorKey {
    fn cmp(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range
            .end
            .cmp(&other.device_range.end)
            .then(self.device_range.start.cmp(&other.device_range.start))
            .then(self.object_id.cmp(&other.object_id))
            .then(self.attribute_id.cmp(&other.attribute_id))
            .then(self.file_offset.cmp(&other.file_offset))
    }
}

impl PartialOrd for AllocatorKey {
    fn partial_cmp(&self, other: &AllocatorKey) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl OrdLowerBound for AllocatorKey {
    fn cmp_lower_bound(&self, other: &AllocatorKey) -> std::cmp::Ordering {
        self.device_range
            .start
            .cmp(&other.device_range.start)
            .then(self.device_range.end.cmp(&other.device_range.end))
            .then(self.object_id.cmp(&other.object_id))
            .then(self.attribute_id.cmp(&other.attribute_id))
            .then(self.file_offset.cmp(&other.file_offset))
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum AllocatorValue {
    Insert,
    Delete,
}

#[derive(Deserialize, Serialize)]
struct AllocatorInfo {
    layers: Vec<u64>,
}

pub struct SimpleAllocator {
    log: Arc<Log>,
    object_id: AtomicU64,
    tree: LSMTree<AllocatorKey, AllocatorValue>,
    reserved_allocations: Arc<SkipListLayer<AllocatorKey, AllocatorValue>>,
    inner: Mutex<Inner>,
}

struct Inner {
    store: Option<Arc<ObjectStore>>,
    info: Option<AllocatorInfo>,
    object_handle: Option<Box<dyn ObjectHandle>>,
    next_block: u64,
    //    reserved_deallocations: RefCell<Rc<SkipListLayer<AllocatorKey, AllocatorValue>>>,
}

impl SimpleAllocator {
    pub fn new(log: Arc<Log>) -> SimpleAllocator {
        SimpleAllocator {
            log,
            // TODO: maybe make tree optional or put all data in separate structure.
            object_id: AtomicU64::new(0),
            tree: LSMTree::new(merge),
            reserved_allocations: Arc::new(SkipListLayer::new(1024)), // TODO: magic numbers
            inner: Mutex::new(Inner {
                store: None,
                info: None,
                object_handle: None,
                next_block: 0,
                //            reserved_deallocations: RefCell::new(Rc::new(SkipListLayer::new(1024))),
            }),
        }
    }
}

impl Allocator for SimpleAllocator {
    fn init(&self, store: Arc<ObjectStore>, handle: Box<dyn ObjectHandle>) {
        println!("allocator object id {:?}", handle.object_id());
        let mut inner = self.inner.lock().unwrap();
        inner.store = Some(store);
        self.object_id.store(handle.object_id(), Relaxed);
        inner.object_handle = Some(handle);
        inner.info = Some(AllocatorInfo { layers: Vec::new() });
    }

    fn open(&self, store: Arc<ObjectStore>, handle: Box<dyn ObjectHandle>) -> Result<(), Error> {
        println!("allocator open object id {:?}", handle.object_id());
        let mut inner = self.inner.lock().unwrap();
        self.object_id.store(handle.object_id(), Relaxed);
        let info: AllocatorInfo = deserialize_from(ObjectHandleCursor::new(handle.borrow(), 0))?;
        println!("done reading allocator file");
        let mut handles = Vec::new();
        for object_id in &info.layers {
            handles.push(store.open_object(*object_id, HandleOptions::default())?);
        }
        inner.info = Some(info);
        self.tree.set_layers(handles.into_boxed_slice());
        inner.store = Some(store);
        inner.object_handle = Some(handle);
        Ok(())
    }

    fn object_id(&self) -> u64 {
        self.object_id.load(Relaxed)
    }

    // TODO: this should return a reservation object rather than just a range so that it can get cleaned up.
    fn allocate(
        &self,
        object_id: u64,
        attribute_id: u64,
        object_range: std::ops::Range<u64>,
    ) -> Result<std::ops::Range<u64>, Error> {
        let len = object_range.end - object_range.start;
        let tree = &self.tree; // TODO: document which tree methods require no locks held.
        let result: std::ops::Range<u64>;
        {
            let mut iter = tree.iter_with_layers(vec![self.reserved_allocations.clone()]);
            let mut last_offset = 0;
            iter.advance()?;
            loop {
                match iter.get() {
                    None => {
                        // TODO: Don't assume infinite device size.
                        result = last_offset..last_offset + len;
                        break;
                    }
                    Some(ItemRef { key: AllocatorKey { device_range, .. }, .. }) => {
                        // println!("found {:?}", device_range);
                        if device_range.start > last_offset {
                            result = last_offset..min(last_offset + len, device_range.start);
                            break;
                        }
                        last_offset = device_range.end;
                    }
                }
                iter.advance()?;
            }
        }
        // println!("alloc: {:?}", result);
        // TODO: got to make reserved allocation actually reserve something.
        self.reserved_allocations.insert(AllocatorItem {
            key: AllocatorKey {
                device_range: result.clone(),
                object_id,
                attribute_id,
                file_offset: result.start,
            },
            value: AllocatorValue::Insert,
        });
        // TODO: roll back reservation if transaction fails.
        Ok(result)
    }

    fn deallocate(
        &self,
        transaction: &mut Transaction,
        object_id: u64,
        attribute_id: u64,
        device_range: std::ops::Range<u64>,
        file_offset: u64,
    ) {
        transaction.add(
            self.object_id(),
            Mutation::Deallocate(AllocatorItem {
                key: AllocatorKey { device_range, object_id, attribute_id, file_offset },
                value: AllocatorValue::Delete,
            }),
        );
    }

    fn set_next_block(&self, block: u64) {
        // TODO Long term, this is wrong --- we need to separate reserved from committed.
        let mut inner = self.inner.lock().unwrap();
        if block > inner.next_block {
            inner.next_block = block;
        }
    }

    fn flush(&self, force: bool) -> Result<(), Error> {
        println!("flushing allocator");
        let mut inner = self.inner.lock().unwrap();
        let mut object_sync = self.log.begin_object_sync(self.object_id());
        if !force && !object_sync.needs_sync() {
            println!("not forced");
            return Ok(());
        }
        // TODO: what if there have been no allocations.
        let mut transaction = Transaction::new();
        let object_handle = inner
            .store
            .as_ref()
            .unwrap()
            .create_object(&mut transaction, HandleOptions::default())?;
        self.log.commit(transaction); // TODO: Move to encompass all of this.
        let object_id = object_handle.object_id();
        // TODO: clean up objects if there's an error.
        inner.info.as_mut().unwrap().layers.push(object_id);
        println!("serializing");
        // let handle = &mut inner.object_handle;
        serialize_into(
            ObjectHandleCursor::new(
                &**inner.object_handle.as_ref().unwrap() as &dyn ObjectHandle,
                0,
            ),
            inner.info.as_ref().unwrap(),
        )?;
        self.tree.commit(object_handle)?;
        object_sync.done();
        println!("allocator flushed");
        Ok(())
    }

    fn commit_allocation(
        &self,
        object_id: u64,
        attribute_id: u64,
        device_range: std::ops::Range<u64>,
        file_offset: u64,
    ) {
        let item = AllocatorItem {
            key: AllocatorKey { device_range, object_id, attribute_id, file_offset },
            value: AllocatorValue::Insert,
        };
        // println!("commit_allocation {:?}", item);
        self.tree.insert(item);
    }

    fn commit_deallocation(&self, item: AllocatorItem) {
        self.reserved_allocations.erase(item.as_item_ref());
        let lower_bound = item.key.lower_bound();
        self.tree.replace_range(item, &lower_bound);
        // TODO: must reserve deallocation until after barrier
    }
}
