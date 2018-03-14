// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fdio;
extern crate fidl;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate mxruntime;
extern crate mxruntime_sys;
extern crate parking_lot;

#[macro_use]
extern crate fuchsia_syslog as syslog;

use app::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::{ClientEnd, InterfacePtr};
use futures::future::ok as fok;
use futures::io;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::prelude::*;
use std::sync::Arc;

// Include the generated FIDL bindings for the `DeviceSetting` service.
extern crate garnet_public_lib_device_settings_fidl;
use garnet_public_lib_device_settings_fidl::{DeviceSettingsManager, DeviceSettingsWatcher, Status,
                                             ValueType};

struct DeviceSettingsManagerServer {
    setting_file_map: HashMap<String, String>,
    watchers: Arc<Mutex<HashMap<String, Vec<DeviceSettingsWatcher::Proxy>>>>,
}

impl DeviceSettingsManagerServer {
    fn initialize_keys(&mut self, data_dir: &str, keys: &[&str]) {
        self.setting_file_map = keys.iter()
            .map(|k| (k.to_string(), format!("{}/{}", data_dir, k.to_lowercase())))
            .collect();
    }

    fn run_watchers(&mut self, key: &str, t: ValueType) {
        let mut map = self.watchers.lock();
        if let Some(m) = map.get_mut(key) {
            m.retain(|w| {
                if let Err(e) = w.on_change_settings(t) {
                    match e {
                        fidl::Error::ClientRead(zx::Status::PEER_CLOSED)
                        | fidl::Error::ClientWrite(zx::Status::PEER_CLOSED) => {
                            return false;
                        }
                        _ => {}
                    };
                    fx_log_err!("Error call watcher: {:?}", e);
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
                fx_log_err!("reading integer: {:?}", e);
                return fok((0, Status::ErrRead));
            }
            Ok(str) => match str.parse::<i64>() {
                Err(_e) => fok((0, Status::ErrIncorrectType)),
                Ok(i) => fok((i, Status::Ok)),
            },
        }
    }

    type SetInteger = fidl::ServerImmediate<bool>;

    fn set_integer(&mut self, key: String, val: i64) -> Self::SetInteger {
        match self.set_key(&key, val.to_string().as_bytes(), ValueType::Int) {
            Ok(r) => return fok(r),
            Err(e) => {
                fx_log_err!("setting integer: {:?}", e);
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
                fx_log_err!("reading string: {:?}", e);
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
                fx_log_err!("setting string: {:?}", e);
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
        match DeviceSettingsWatcher::new_proxy(watcher.inner) {
            Err(e) => {
                fx_log_err!("getting watcher proxy: {:?}", e);
                return fok(Status::ErrUnknown);
            }
            Ok(w) => {
                let mut map = self.watchers.lock();
                let mv = map.entry(key).or_insert(Vec::new());
                mv.push(w);
                fok(Status::Ok)
            }
        }
    }
}

fn main() {
    if let Err(e) = main_ds() {
        fx_log_err!("{:?}", e);
    }
}

fn main_ds() -> Result<(), Error> {
    syslog::init_with_tags(&["device_settings"])?;
    let mut core = async::Executor::new().context("unable to create executor")?;

    let watchers = Arc::new(Mutex::new(HashMap::new()));
    // Attempt to create data directory
    fs::create_dir_all(DATA_DIR).context("creating directory")?;

    let server = ServicesServer::new()
        .add_service(move || {
            let mut d = DeviceSettingsManagerServer {
                setting_file_map: HashMap::new(),
                watchers: watchers.clone(),
            };

            d.initialize_keys(DATA_DIR, &["Timezone", "TestSetting"]);

            DeviceSettingsManager::Dispatcher(d)
        })
        .start()
        .map_err(|e| e.context("error starting service server"))?;

    Ok(core.run(server, /* threads */ 2).context("running server")?)
}

#[cfg(test)]
mod tests {
    extern crate tempdir;

    use self::tempdir::TempDir;
    use futures::prelude::*;
    use super::*;

    use garnet_public_lib_device_settings_fidl::DeviceSettingsManager::Server;

    fn async_test<F, Fut>(keys: &[&str], f: F)
    where
        F: FnOnce(DeviceSettingsManagerServer) -> Fut,
        Fut: Future<Item = (), Error = fidl::CloseChannel>,
    {
        let (mut exec, device_settings, _t) = setup(keys).expect("Setup should not have failed");

        let test_fut = f(device_settings);

        exec.run_singlethreaded(test_fut)
            .expect("executor run failed");
    }

    fn setup(keys: &[&str]) -> Result<(async::Executor, DeviceSettingsManagerServer, TempDir), ()> {
        let exec = async::Executor::new().unwrap();
        let mut device_settings = DeviceSettingsManagerServer {
            setting_file_map: HashMap::new(),
            watchers: Arc::new(Mutex::new(HashMap::new())),
        };
        let tmp_dir = TempDir::new("ds_test").unwrap();

        device_settings.initialize_keys(tmp_dir.path().to_str().unwrap(), keys);

        // return tmp_dir to keep it in scope
        return Ok((exec, device_settings, tmp_dir));
    }

    #[test]
    fn test_int() {
        async_test(&["TestKey"], |mut device_settings| {
            device_settings
                .set_integer("TestKey".to_string(), 18)
                .and_then(move |response| {
                    assert!(response, "set_integer failed");
                    device_settings.get_integer("TestKey".to_string())
                })
                .and_then(move |response| {
                    assert_eq!(response, (18, Status::Ok));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_string() {
        async_test(&["TestKey"], |mut device_settings| {
            device_settings
                .set_string("TestKey".to_string(), "mystring".to_string())
                .and_then(move |response| {
                    assert!(response, "set_string failed");
                    device_settings.get_string("TestKey".to_string())
                })
                .and_then(move |response| {
                    assert_eq!(response, ("mystring".to_string(), Status::Ok));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_invalid_key() {
        async_test(&[], |mut device_settings| {
            device_settings
                .get_string("TestKey".to_string())
                .and_then(move |response| {
                    assert_eq!(response, ("".to_string(), Status::ErrInvalidSetting));
                    device_settings.get_integer("TestKey".to_string())
                })
                .and_then(move |response| {
                    assert_eq!(response, (0, Status::ErrInvalidSetting));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_incorrect_type() {
        async_test(&["TestKey"], |mut device_settings| {
            device_settings
                .set_string("TestKey".to_string(), "mystring".to_string())
                .and_then(move |response| {
                    assert!(response, "set_string failed");
                    device_settings.get_integer("TestKey".to_string())
                })
                .and_then(move |response| {
                    assert_eq!(response, (0, Status::ErrIncorrectType));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_not_set_err() {
        async_test(&["TestKey"], |mut device_settings| {
            device_settings
                .get_integer("TestKey".to_string())
                .and_then(move |response| {
                    assert_eq!(response, (0, Status::ErrNotSet));
                    device_settings.get_string("TestKey".to_string())
                })
                .and_then(move |response| {
                    assert_eq!(response, ("".to_string(), Status::ErrNotSet));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_multiple_keys() {
        async_test(&["TestKey1", "TestKey2"], |mut device_settings| {
            device_settings
                .set_integer("TestKey1".to_string(), 18)
                .and_then(move |response| {
                    assert!(response, "set_integer failed");
                    device_settings
                        .set_string("TestKey2".to_string(), "mystring".to_string())
                        .map(move |response| (response, device_settings))
                })
                .and_then(|(response, mut device_settings)| {
                    assert!(response, "set_string failed");
                    device_settings
                        .get_integer("TestKey1".to_string())
                        .map(move |response| (response, device_settings))
                })
                .and_then(|(response, mut device_settings)| {
                    assert_eq!(response, (18, Status::Ok));
                    device_settings.get_string("TestKey2".to_string())
                })
                .and_then(|response| {
                    assert_eq!(response, ("mystring".to_string(), Status::Ok));
                    Ok(())
                })
        });
    }
}
