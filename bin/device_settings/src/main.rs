// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate mxruntime;
extern crate mxruntime_sys;
extern crate tokio_core;
extern crate tokio_fuchsia;
extern crate fdio;

use failure::{Error, ResultExt};
use fuchsia_app::server::bootstrap_server;
use fidl::{InterfacePtr, ClientEnd};
use futures::future::ok as fok;
use std::cell::RefCell;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::prelude::*;
use std::io;
use std::rc::Rc;
use tokio_core::reactor;

// Include the generated FIDL bindings for the `DeviceSetting` service.
extern crate garnet_public_lib_device_settings_fidl;
use garnet_public_lib_device_settings_fidl::{DeviceSettingsManager, Status, DeviceSettingsWatcher,
                                             ValueType};

struct DeviceSettingsManagerServer {
    setting_file_map: HashMap<String, String>,
    watchers: Rc<RefCell<HashMap<String, Vec<DeviceSettingsWatcher::Proxy>>>>,
    handle: reactor::Handle,
}

impl DeviceSettingsManagerServer {
    fn initialize_keys(&mut self, data_dir: &str, keys: &[&str]) {
        self.setting_file_map = keys.iter()
            .map(|k| {
                (k.to_string(), format!("{}/{}", data_dir, k.to_lowercase()))
            })
            .collect();
    }

    fn run_watchers(&mut self, key: &str, t: ValueType) {
        let mut map = self.watchers.borrow_mut();
        if let Some(m) = map.get_mut(key) {
            m.retain(|w| {
                if let Err(e) = w.on_change_settings(t) {
                    match e {
                        fidl::Error::ClientRead(ref ie) |
                        fidl::Error::ClientWrite(ref ie) => {
                            if ie.kind() == io::ErrorKind::ConnectionAborted {
                                return false;
                            }
                        }
                        _ => {}
                    };
                    eprintln!("Error call watcher: {:?}", e);
                }
                return true;
            });
        }
    }

    fn set_key(&mut self, key: &str, buf: &[u8], t: ValueType) -> io::Result<bool> {
        match self.setting_file_map.get(key) {
            Some(file) => write_to_file(file, buf)?,
            None => return Ok(false),
        };

        self.run_watchers(&key, t);
        Ok(true)
    }
}

static DATA_DIR: &'static str = "/data/device-settings";

fn write_to_file(file: &str, buf: &[u8]) -> io::Result<()> {
    let mut f = File::create(file)?;
    f.write_all(buf)
}

fn read_file(file: &str) -> io::Result<String> {
    let mut f = File::open(file)?;
    let mut contents = String::new();
    if let Err(e) = f.read_to_string(&mut contents) {
        return Err(e);
    }
    Ok(contents)
}

impl DeviceSettingsManager::Server for DeviceSettingsManagerServer {
    type GetInteger = fidl::ServerImmediate<(i64, Status)>;

    fn get_integer(&mut self, key: String) -> Self::GetInteger {
        let file = if let Some(f) = self.setting_file_map.get(&key) {
            f
        } else {
            return fok((0, Status::ErrInvalidSetting));
        };
        match read_file(file) {
            Err(e) => {
                if e.kind() == io::ErrorKind::NotFound {
                    return fok((0, Status::ErrNotSet));
                }
                eprintln!("DeviceSetting: Error reading integer: {:?}", e);
                return fok((0, Status::ErrRead));
            }
            Ok(str) => {
                match str.parse::<i64>() {
                    Err(_e) => fok((0, Status::ErrIncorrectType)),
                    Ok(i) => fok((i, Status::Ok)),
                }
            }
        }
    }

    type SetInteger = fidl::ServerImmediate<bool>;

    fn set_integer(&mut self, key: String, val: i64) -> Self::SetInteger {
        match self.set_key(&key, val.to_string().as_bytes(), ValueType::Int) {
            Ok(r) => return fok(r),
            Err(e) => {
                eprintln!("DeviceSetting: Error setting integer: {:?}", e);
                return fok(false);
            }
        }
    }

    type GetString = fidl::ServerImmediate<(String, Status)>;

    fn get_string(&mut self, key: String) -> Self::GetString {
        let file = if let Some(f) = self.setting_file_map.get(&key) {
            f
        } else {
            return fok(("".to_string(), Status::ErrInvalidSetting));
        };
        match read_file(file) {
            Err(e) => {
                if e.kind() == io::ErrorKind::NotFound {
                    return fok(("".to_string(), Status::ErrNotSet));
                }
                eprintln!("DeviceSetting: Error reading string: {:?}", e);
                return fok(("".to_string(), Status::ErrRead));
            }
            Ok(s) => fok((s, Status::Ok)),
        }
    }

    type SetString = fidl::ServerImmediate<bool>;

    fn set_string(&mut self, key: String, val: String) -> Self::SetString {
        match self.set_key(&key, val.as_bytes(), ValueType::String) {
            Ok(r) => return fok(r),
            Err(e) => {
                eprintln!("DeviceSetting: Error setting string: {:?}", e);
                return fok(false);
            }
        }

    }

    type Watch = fidl::ServerImmediate<Status>;
    fn watch(
        &mut self,
        key: String,
        watcher: InterfacePtr<ClientEnd<DeviceSettingsWatcher::Service>>,
    ) -> Self::Watch {
        if !self.setting_file_map.contains_key(&key) {
            return fok(Status::ErrInvalidSetting);
        }
        match DeviceSettingsWatcher::new_proxy(watcher.inner, &self.handle) {
            Err(e) => {
                eprintln!("DeviceSetting: Error getting watcher proxy: {:?}", e);
                return fok(Status::ErrUnknown);
            }
            Ok(w) => {
                let mut map = self.watchers.borrow_mut();
                let mv = map.entry(key).or_insert(Vec::new());
                mv.push(w);
                fok(Status::Ok)
            }
        }

    }
}

fn main() {
    if let Err(e) = main_ds() {
        eprintln!("DeviceSetting: Error: {:?}", e);
    }
}

// TODO(anmittal): Use log crate and use that for logging
fn main_ds() -> Result<(), Error> {
    let mut core = reactor::Core::new().context("unable to create core")?;
    let handle = core.handle();

    let watchers = Rc::new(RefCell::new(HashMap::new()));
    // Attempt to create data directory
    fs::create_dir_all(DATA_DIR).context("creating directory")?;
    let server = bootstrap_server(handle.clone(), move || {
        let mut d = DeviceSettingsManagerServer {
            setting_file_map: HashMap::new(),
            watchers: watchers.clone(),
            handle: handle.clone(),
        };

        d.initialize_keys(DATA_DIR, &["Timezone", "TestSetting"]);

        DeviceSettingsManager::Dispatcher(d)
    }).context("running server")?;

    Ok(core.run(server).context("running server")?)
}

#[cfg(test)]
mod tests {
    extern crate tempdir;

    use self::tempdir::TempDir;
    use super::*;

    use garnet_public_lib_device_settings_fidl::DeviceSettingsManager::Server;


    fn setup(
        keys: &[&str],
    ) -> Result<(tokio_core::reactor::Core, DeviceSettingsManagerServer, TempDir), ()> {
        let core = reactor::Core::new().unwrap();
        let mut device_settings = DeviceSettingsManagerServer {
            setting_file_map: HashMap::new(),
            watchers: Rc::new(RefCell::new(HashMap::new())),
            handle: core.handle(),
        };
        let tmp_dir = TempDir::new("ds_test").unwrap();


        device_settings.initialize_keys(tmp_dir.path().to_str().unwrap(), keys);

        // return tmp_dir to keep it in scope
        return Ok((core, device_settings, tmp_dir));
    }

    #[test]
    fn test_int() {
        let (mut core, mut device_settings, _t) =
            setup(&["TestKey"]).expect("Setup should not have failed");

        let response_fut = device_settings.set_integer("TestKey".to_string(), 18);
        let response = core.run(response_fut).unwrap();
        assert!(response, "set_integer failed");

        let response_fut = device_settings.get_integer("TestKey".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, (18, Status::Ok));
    }

    #[test]
    fn test_string() {
        let (mut core, mut device_settings, _t) =
            setup(&["TestKey"]).expect("Setup should not have failed");

        let response_fut =
            device_settings.set_string("TestKey".to_string(), "mystring".to_string());
        let response = core.run(response_fut).unwrap();
        assert!(response, "set_string failed");

        let response_fut = device_settings.get_string("TestKey".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, ("mystring".to_string(), Status::Ok));
    }

    #[test]
    fn test_invalid_key() {
        let (mut core, mut device_settings, _t) = setup(&[]).expect("Setup should not have failed");

        let response_fut = device_settings.get_string("TestKey".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, ("".to_string(), Status::ErrInvalidSetting));

        let response_fut = device_settings.get_integer("TestKey".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, (0, Status::ErrInvalidSetting));
    }

    #[test]
    fn test_incorrect_type() {
        let (mut core, mut device_settings, _t) =
            setup(&["TestKey"]).expect("Setup should not have failed");

        let response_fut =
            device_settings.set_string("TestKey".to_string(), "mystring".to_string());
        let response = core.run(response_fut).unwrap();
        assert!(response, "set_string failed");

        let response_fut = device_settings.get_integer("TestKey".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, (0, Status::ErrIncorrectType));
    }

    #[test]
    fn test_not_set_err() {
        let (mut core, mut device_settings, _t) =
            setup(&["TestKey"]).expect("Setup should not have failed");

        let response_fut = device_settings.get_integer("TestKey".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, (0, Status::ErrNotSet));

        let response_fut = device_settings.get_string("TestKey".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, ("".to_string(), Status::ErrNotSet));
    }

    #[test]
    fn test_multiple_keys() {
        let (mut core, mut device_settings, _t) =
            setup(&["TestKey1", "TestKey2"]).expect("Setup should not have failed");

        let response_fut = device_settings.set_integer("TestKey1".to_string(), 18);
        let response = core.run(response_fut).unwrap();
        assert!(response, "set_integer failed");

        let response_fut =
            device_settings.set_string("TestKey2".to_string(), "mystring".to_string());
        let response = core.run(response_fut).unwrap();
        assert!(response, "set_string failed");

        let response_fut = device_settings.get_integer("TestKey1".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, (18, Status::Ok));

        let response_fut = device_settings.get_string("TestKey2".to_string());
        let response = core.run(response_fut).expect(
            "core.run should not have failed",
        );
        assert_eq!(response, ("mystring".to_string(), Status::Ok));
    }
}
