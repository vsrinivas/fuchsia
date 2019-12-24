// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The store can be serialized and deserialized as a custom form of tlv:
//! https://en.wikipedia.org/wiki/Type-length-value
//!
//! It would be simpler here to simply pull in an external library to do something like json
//! serialization, but this implementation is much less likely to cause security concerns due to the
//! reduced functionality of the code.

use anyhow::{format_err, Error};
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use fidl_fuchsia_mem;
use fidl_fuchsia_stash::{KeyValue, ListItem, Value, ValueType};
use fuchsia_syslog::fx_log_info;
use fuchsia_zircon as zx;
use std::collections::HashMap;
use std::fs;
use std::io::{Cursor, ErrorKind, Read, Write};
use std::path::PathBuf;

pub type ClientName = String;
pub type Key = String;

/// Clones a Value reference into a new Value. Can fail if the syscalls to clone a vmo fail.
/// We need to be able to clone Value to use it directly in the store, but both the Clone trait and
/// Value struct are external, so we can't impl Clone for Value.
pub fn clone_value(v: &Value) -> Result<Value, Error> {
    Ok(match v {
        Value::Intval(x) => Value::Intval(x.clone()),
        Value::Floatval(x) => Value::Floatval(x.clone()),
        Value::Boolval(x) => Value::Boolval(x.clone()),
        Value::Stringval(x) => Value::Stringval(x.clone()),
        Value::Bytesval(x) => Value::Bytesval(clone_buffer(x)?),
    })
}

/// Creates a copy-on-write clone of the given fidl buffer
pub fn clone_buffer(b: &fidl_fuchsia_mem::Buffer) -> Result<fidl_fuchsia_mem::Buffer, Error> {
    let new_vmo = b
        .vmo
        .create_child(zx::VmoChildOptions::COPY_ON_WRITE, 0, b.size)
        .map_err(|s| format_err!(format!("error cloning buffer, zx status: {}", s)))?;
    Ok(fidl_fuchsia_mem::Buffer { vmo: new_vmo, size: b.size })
}

/// Store is the struct representing the contents of the backing tlv file
#[derive(Debug, PartialEq, Default)]
pub struct Store {
    data: HashMap<ClientName, HashMap<Key, Value>>,
}

fn value_type_as_bytes(v: &Value) -> &[u8] {
    match v {
        Value::Intval(_) => &[0x00],
        Value::Floatval(_) => &[0x01],
        Value::Boolval(_) => &[0x02],
        Value::Stringval(_) => &[0x03],
        Value::Bytesval(_) => &[0x04],
    }
}

fn value_into_bytes(v: &Value) -> Result<Vec<u8>, Error> {
    let mut wrt = vec![];
    match v {
        Value::Intval(i) => wrt.write_i64::<LittleEndian>(*i)?,
        Value::Floatval(f) => wrt.write_f64::<LittleEndian>(*f)?,
        Value::Boolval(false) => wrt.write_u8(0x00)?,
        Value::Boolval(true) => wrt.write_u8(0x01)?,
        Value::Stringval(s) => wrt.append(&mut s.clone().into_bytes()),
        Value::Bytesval(b) => {
            let mut buf = vec![0; b.size as usize];
            b.vmo
                .read(&mut buf, 0)
                .map_err(|s| format_err!(format!("error reading buffer, zx status: {}", s)))?;
            wrt.append(&mut buf);
        }
    }
    Ok(wrt)
}
fn value_from_bytes(typ: u8, bytes: Vec<u8>) -> Result<Value, Error> {
    match typ {
        0x00 => Ok(Value::Intval(Cursor::new(bytes).read_i64::<LittleEndian>()?)),
        0x01 => Ok(Value::Floatval(Cursor::new(bytes).read_f64::<LittleEndian>()?)),
        0x02 => match Cursor::new(bytes).read_u8()? {
            0x00 => Ok(Value::Boolval(false)),
            0x01 => Ok(Value::Boolval(true)),
            b => Err(format_err!(format!("unknown bool value: {}", b))),
        },
        0x03 => Ok(Value::Stringval(String::from_utf8(bytes)?)),
        0x04 => {
            let vmo = zx::Vmo::create(bytes.len() as u64)
                .map_err(|s| format_err!(format!("error creating buffer, zx status: {}", s)))?;
            vmo.write(&bytes, 0)
                .map_err(|s| format_err!(format!("error writing buffer, zx status: {}", s)))?;
            Ok(Value::Bytesval(fidl_fuchsia_mem::Buffer { vmo: vmo, size: bytes.len() as u64 }))
        }
        t => Err(format_err!(format!("unknown type: {}", t))),
    }
}

impl Store {
    fn serialize(&self) -> Result<Vec<u8>, Error> {
        let mut res = vec![];
        for (client_name, client_data) in self.data.iter() {
            let mut client_name_length = vec![];
            client_name_length.write_u64::<LittleEndian>(client_name.len() as u64)?;
            let mut client_name_bytes = client_name.clone().into_bytes();

            let mut client_entries = vec![];
            client_entries.write_u64::<LittleEndian>(client_data.len() as u64)?;

            res.append(&mut client_name_length);
            res.append(&mut client_name_bytes);
            res.append(&mut client_entries);

            for (key, val) in client_data {
                let mut key_length = vec![];
                key_length.write_u64::<LittleEndian>(key.len() as u64)?;
                let mut key_bytes = key.clone().into_bytes();
                let val_type = value_type_as_bytes(val);
                let mut val_bytes = value_into_bytes(val)?;
                let mut val_length = vec![];
                val_length.write_u64::<LittleEndian>(val_bytes.len() as u64)?;

                res.append(&mut key_length);
                res.append(&mut key_bytes);
                res.extend_from_slice(val_type);
                res.append(&mut val_length);
                res.append(&mut val_bytes);
            }
        }
        Ok(res)
    }

    fn deserialize(bytes: Vec<u8>) -> Result<Store, Error> {
        let mut res = Store::default();
        let bytes_len = bytes.len() as u64;
        let mut cursor = Cursor::new(bytes);
        while cursor.position() < bytes_len {
            let client_name_length = cursor.read_u64::<LittleEndian>()?;
            let mut client_name = Vec::new();
            client_name.resize(client_name_length as usize, 0);
            cursor.read(&mut client_name[..])?;

            let client_entries = cursor.read_u64::<LittleEndian>()?;

            let mut client_data = HashMap::new();
            for _ in 0..client_entries {
                let key_length = cursor.read_u64::<LittleEndian>()?;
                let mut key_bytes = Vec::new();
                key_bytes.resize(key_length as usize, 0);
                cursor.read(&mut key_bytes[..])?;

                let mut val_type = [0; 1];
                cursor.read(&mut val_type[..])?;
                let val_type = val_type[0];

                let val_length = cursor.read_u64::<LittleEndian>()?;
                let mut val_bytes = Vec::new();
                val_bytes.resize(val_length as usize, 0);
                cursor.read(&mut val_bytes[..])?;

                client_data
                    .insert(String::from_utf8(key_bytes)?, value_from_bytes(val_type, val_bytes)?);
            }

            res.data.insert(String::from_utf8(client_name)?, client_data);
        }
        Ok(res)
    }
}

pub fn value_to_type(v: &Value) -> ValueType {
    match v {
        Value::Intval(_) => ValueType::IntVal,
        Value::Floatval(_) => ValueType::FloatVal,
        Value::Boolval(_) => ValueType::BoolVal,
        Value::Stringval(_) => ValueType::StringVal,
        Value::Bytesval(_) => ValueType::BytesVal,
    }
}

/// StoreManager is used to hold and manipulate a Store, writing changes to disk when appropriate.
pub struct StoreManager {
    backing_file: PathBuf,
    store: Store,
}

impl StoreManager {
    /// Create a new StoreManager, loading the store from backing_file if it exists, and writing
    /// changes to the store into backing_file.
    pub fn new<P: Into<PathBuf>>(backing_file: P) -> Result<StoreManager, Error> {
        let backing_file = backing_file.into();
        let store = match fs::read(&backing_file) {
            Ok(buf) => {
                fx_log_info!("deserializing store from file");
                Store::deserialize(buf)?
            }
            Err(e) => {
                if e.kind() != ErrorKind::NotFound {
                    return Err(format_err!(format!("{}", e)));
                }
                fx_log_info!("store file doesn't exist, using empty store");
                Store::default()
            }
        };

        Ok(StoreManager { backing_file, store })
    }

    fn save_store(&self) -> Result<(), Error> {
        let temp_file_path = self.backing_file.with_extension("tmp");
        let store_contents = self.store.serialize()?;
        {
            let mut f = fs::OpenOptions::new()
                .write(true)
                .truncate(true)
                .create(true)
                .open(temp_file_path.clone())?;

            f.write_all(&store_contents)?;
        }
        fs::rename(temp_file_path, &self.backing_file)?;
        Ok(())
    }

    /// Gets a value from the store. Returns None if the value is unset.
    pub fn get_value(&self, client_name: &str, key: &str) -> Option<&Value> {
        match self.store.data.get(client_name) {
            None => None,
            Some(client_fields) => client_fields.get(key),
        }
    }

    /// Sets a value in the store.
    pub fn set_value(&mut self, client_name: &str, key: String, field: Value) -> Result<(), Error> {
        let client_fields =
            self.store.data.entry(client_name.to_string()).or_insert(HashMap::new());
        client_fields.insert(key, field);
        self.save_store()
    }

    /// Delete a value from the store. Will return Ok(true) for a successful deletion, Ok(false) if
    /// the field doesn't exist, and Err(e) on any unexpected errors.
    pub fn delete_value(&mut self, client_name: &str, key: &str) -> Result<(), Error> {
        let client_fields =
            self.store.data.entry(client_name.to_string()).or_insert(HashMap::new());
        client_fields.remove(key);
        self.save_store()
    }

    /// Retrieves a list of key/type pairs for all keys containing the given prefix.
    pub fn list_prefix(&self, client_name: &str, prefix: &str) -> Vec<ListItem> {
        let o_client_fields = self.store.data.get(client_name);
        if o_client_fields.is_none() {
            return vec![];
        }
        let client_fields = o_client_fields.unwrap();
        let mut result = vec![];
        for (k, v) in client_fields.iter() {
            if k.starts_with(prefix) {
                result.push(ListItem { key: k.clone(), type_: value_to_type(&v) });
            }
        }
        result
    }

    /// Retrieves a list of key/value pairs for all keys containing the given prefix.
    pub fn get_prefix(&self, client_name: &str, prefix: &str) -> Result<Vec<KeyValue>, Error> {
        let o_client_fields = self.store.data.get(client_name);
        if o_client_fields.is_none() {
            return Ok(vec![]);
        }
        let client_fields = o_client_fields.unwrap();
        let mut res = Vec::new();
        for (k, v) in client_fields.iter() {
            if k.starts_with(prefix) {
                res.push(KeyValue { key: k.clone(), val: clone_value(v)? });
            }
        }
        Ok(res)
    }
}

#[cfg(test)]
mod tests {
    use crate::store::*;
    use std::io;
    use tempfile::TempDir;

    fn get_tmp_store_manager(tmp_dir: &TempDir) -> StoreManager {
        StoreManager::new(tmp_dir.path().join("stash.store")).unwrap()
    }

    #[test]
    fn test_get_value() {
        let tmp_dir = TempDir::new().unwrap();
        let mut sm = get_tmp_store_manager(&tmp_dir);

        let test_client_name = "test_client".to_owned();
        let test_key = "test_key".to_owned();
        let test_field = Value::Boolval(true);

        let mut client_data = HashMap::new();
        client_data.insert(test_key.clone(), clone_value(&test_field).unwrap());

        sm.store.data.insert(test_client_name.clone(), client_data);
        sm.save_store().unwrap();

        let fetched_field = sm.get_value(&test_client_name, &test_key);
        assert_eq!(fetched_field, Some(&test_field));
    }

    #[test]
    fn test_get_nonexistent_value() {
        let tmp_dir = TempDir::new().unwrap();
        let sm = get_tmp_store_manager(&tmp_dir);

        let test_client_name = "test_client".to_owned();
        let test_key = "test_key".to_owned();

        let fetched_field = sm.get_value(&test_client_name, &test_key);
        assert_eq!(fetched_field, None);
    }

    #[test]
    fn test_set_value() {
        let tmp_dir = TempDir::new().unwrap();
        let mut sm = get_tmp_store_manager(&tmp_dir);

        let test_client_name = "test_client".to_owned();
        let test_key = "test_key".to_owned();
        let test_field = Value::Boolval(true);

        sm.set_value(&test_client_name, test_key.clone(), clone_value(&test_field).unwrap())
            .unwrap();

        let saved_field = sm.store.data.get(&test_client_name).unwrap().get(&test_key).unwrap();

        assert_eq!(saved_field, &test_field);
    }

    #[test]
    fn test_delete_value() {
        let tmp_dir = TempDir::new().unwrap();
        let mut sm = get_tmp_store_manager(&tmp_dir);

        let test_client_name = "test_client".to_owned();
        let test_key = "test_key".to_owned();
        let test_field = Value::Boolval(true);

        let mut client_data = HashMap::new();
        client_data.insert(test_key.clone(), clone_value(&test_field).unwrap());

        sm.store.data.insert(test_client_name.clone(), client_data);
        sm.save_store().unwrap();

        sm.delete_value(&test_client_name, &test_key).unwrap();
        assert_eq!(None, sm.store.data.get(&test_client_name).unwrap().get(&test_key));
    }

    #[test]
    fn test_list_prefix() {
        let tmp_dir = TempDir::new().unwrap();
        let mut sm = get_tmp_store_manager(&tmp_dir);

        let test_client_name = "test_client".to_owned();
        let test_field = Value::Boolval(true);

        let mut client_data = HashMap::new();
        client_data.insert("a".to_string(), clone_value(&test_field).unwrap());
        client_data.insert("a/a".to_string(), clone_value(&test_field).unwrap());
        client_data.insert("b".to_string(), clone_value(&test_field).unwrap());

        sm.store.data.insert(test_client_name.clone(), client_data);
        sm.save_store().unwrap();

        let res = sm.list_prefix(&test_client_name, &"a".to_string());
        assert_eq!(2, res.len());
        assert_eq!(
            true,
            res.contains(&ListItem { key: "a".to_string(), type_: ValueType::BoolVal })
        );
        assert_eq!(
            true,
            res.contains(&ListItem { key: "a/a".to_string(), type_: ValueType::BoolVal })
        );
    }

    #[test]
    fn test_get_prefix() {
        let tmp_dir = TempDir::new().unwrap();
        let mut sm = get_tmp_store_manager(&tmp_dir);

        let test_client_name = "test_client".to_owned();
        let test_field = Value::Boolval(true);

        let mut client_data = HashMap::new();
        client_data.insert("a".to_string(), clone_value(&test_field).unwrap());
        client_data.insert("a/a".to_string(), clone_value(&test_field).unwrap());
        client_data.insert("b".to_string(), clone_value(&test_field).unwrap());

        sm.store.data.insert(test_client_name.clone(), client_data);
        sm.save_store().unwrap();

        let res = sm.get_prefix(&test_client_name, &"a".to_string()).unwrap();
        assert_eq!(2, res.len());
        assert_eq!(
            true,
            res.contains(&KeyValue { key: "a".to_string(), val: Value::Boolval(true) })
        );
        assert_eq!(
            true,
            res.contains(&KeyValue { key: "a/a".to_string(), val: Value::Boolval(true) })
        );
    }

    #[test]
    fn test_serialize_deserialize() {
        let tmp_dir = TempDir::new().unwrap();
        let backing_file = tmp_dir.path().join("file.store");
        let test_client = "test_client";

        let mut store = Store::default();
        store.data.insert(test_client.to_string(), HashMap::new());
        store
            .data
            .get_mut(&test_client.to_string())
            .unwrap()
            .insert("int".to_string(), Value::Intval(1));
        store
            .data
            .get_mut(&test_client.to_string())
            .unwrap()
            .insert("float".to_string(), Value::Floatval(1.0));
        store
            .data
            .get_mut(&test_client.to_string())
            .unwrap()
            .insert("bool true".to_string(), Value::Boolval(true));
        store
            .data
            .get_mut(&test_client.to_string())
            .unwrap()
            .insert("bool false".to_string(), Value::Boolval(false));
        store
            .data
            .get_mut(&test_client.to_string())
            .unwrap()
            .insert("string".to_string(), Value::Stringval("test string".to_string()));
        // Value::Bytesval can't be tested here because the vmo handle number changes during the
        // clone operation, making the assert_eq! below fail.

        let clone_store = |_data: &Store| {
            let mut res = HashMap::new();
            for (client_name, client_fields) in store.data.iter() {
                let mut client_data = HashMap::new();
                for (k, v) in client_fields.iter() {
                    client_data.insert(k.clone(), clone_value(v).unwrap());
                }
                res.insert(client_name.clone(), client_data);
            }
            Store { data: res }
        };

        let mut sm_writer = StoreManager::new(backing_file.clone())
            .expect("couldn't make store manager for writing");
        sm_writer.store = clone_store(&store);
        sm_writer.save_store().expect("couldn't save store");

        let sm_reader = StoreManager::new(backing_file.clone())
            .expect("couldn't make store manager from pre-populated file");

        assert_eq!(store, sm_writer.store);
        assert_eq!(store, sm_reader.store);
    }

    #[test]
    fn test_comprehensive() {
        let tmp_dir = TempDir::new().unwrap();
        let mut sm = get_tmp_store_manager(&tmp_dir);

        let test_client_name = "test_client".to_owned();
        let test_key = "test_key".to_owned();
        let test_field = Value::Boolval(true);

        sm.set_value(&test_client_name, test_key.clone(), clone_value(&test_field).unwrap())
            .unwrap();

        assert_eq!(Some(&test_field), sm.get_value(&test_client_name, &test_key));
        assert_eq!(
            vec![ListItem { key: test_key.clone(), type_: ValueType::BoolVal }],
            sm.list_prefix(&test_client_name, &"".to_string())
        );
        assert_eq!(
            vec![KeyValue { key: test_key.clone(), val: Value::Boolval(true) }],
            sm.get_prefix(&test_client_name, &"".to_string()).unwrap()
        );

        sm.delete_value(&test_client_name, &test_key).unwrap();

        assert_eq!(None, sm.get_value(&test_client_name, &test_key));
        assert_eq!(vec![] as Vec<ListItem>, sm.list_prefix(&test_client_name, &"".to_string()));
        assert_eq!(
            vec![] as Vec<KeyValue>,
            sm.get_prefix(&test_client_name, &"".to_string()).unwrap()
        );
    }

    #[test]
    fn test_file_is_updated() {
        let tmp_dir = TempDir::new().unwrap();
        let mut sm = get_tmp_store_manager(&tmp_dir);

        let test_client_name = "test_client".to_owned();
        let file_path = sm.backing_file.clone();

        let mut curr_file_size = 0;
        let mut new_file_size;

        // File shouldn't exist when we start
        assert_eq!(fs::metadata(&file_path).unwrap_err().kind(), io::ErrorKind::NotFound);

        // Set a field, the file size should increase
        sm.set_value(&test_client_name, "foo".to_string(), Value::Boolval(true)).unwrap();
        sm.save_store().unwrap();

        new_file_size = fs::metadata(&file_path).unwrap().len();
        assert!(curr_file_size < new_file_size);
        curr_file_size = new_file_size;

        // Set a different field, the file size should increase
        sm.set_value(&test_client_name, "bar".to_string(), Value::Boolval(true)).unwrap();
        sm.save_store().unwrap();

        new_file_size = fs::metadata(&file_path).unwrap().len();
        assert!(curr_file_size < new_file_size);
        curr_file_size = new_file_size;

        // Delete a field, the file size should decrease
        sm.delete_value(&test_client_name, "bar").unwrap();
        sm.save_store().unwrap();

        new_file_size = fs::metadata(&file_path).unwrap().len();
        assert!(curr_file_size > new_file_size);
        curr_file_size = new_file_size;

        // Delete a field, the file size should decrease
        sm.delete_value(&test_client_name, "foo").unwrap();
        sm.save_store().unwrap();

        new_file_size = fs::metadata(&file_path).unwrap().len();
        assert!(curr_file_size > new_file_size);
    }
}
