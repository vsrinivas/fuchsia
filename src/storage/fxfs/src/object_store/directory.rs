use {
    crate::{
        object_handle::ObjectHandle,
        object_store::{
            log::{Mutation, Transaction},
            map_to_io_error,
            record::{ObjectItem, ObjectKey, ObjectType, ObjectValue},
            HandleOptions, ObjectStore,
        },
    },
    std::{io::ErrorKind, sync::Arc},
};

pub struct Directory {
    store: Arc<ObjectStore>,
    object_id: u64,
}

impl<'a> Directory {
    pub fn new(store: Arc<ObjectStore>, object_id: u64) -> Self {
        Directory { store, object_id }
    }

    pub fn create_child_file(&mut self, name: &str) -> std::io::Result<impl ObjectHandle> {
        let mut transaction = Transaction::new(); // TODO: transaction too big?
        let handle = self.store.create_object(&mut transaction, HandleOptions::default())?;
        transaction.add(
            self.store.store_object_id(),
            Mutation::Insert {
                item: ObjectItem {
                    key: ObjectKey::child(self.object_id, name),
                    value: ObjectValue::child(handle.object_id(), ObjectType::File),
                },
            },
        );
        self.store.log.upgrade().unwrap().commit(transaction);
        Ok(handle)
    }

    pub fn object_id(&self) -> u64 {
        return self.object_id;
    }

    pub fn lookup(&self, name: &str) -> std::io::Result<(u64, ObjectType)> {
        let item = self
            .store
            .tree()
            .find(&ObjectKey::child(self.object_id, name))
            .map_err(map_to_io_error)?
            .ok_or(std::io::Error::new(ErrorKind::NotFound, "Not found"))?;
        if let ObjectValue::Child { object_id, object_type } = item.value {
            Ok((object_id, object_type))
        } else {
            Err(std::io::Error::new(ErrorKind::InvalidData, "Expected child"))
        }
    }
}
