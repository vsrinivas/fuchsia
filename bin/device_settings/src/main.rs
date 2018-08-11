// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// #![deny(warnings)]

extern crate failure;
extern crate fdio;
extern crate fidl;
extern crate fidl_fuchsia_devicesettings;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
#[macro_use]
extern crate fuchsia_syslog as syslog;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate mxruntime;
extern crate mxruntime_sys;
extern crate parking_lot;


use app::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::{RequestStream, ServiceMarker};
use futures::prelude::*;
use futures::future::{FutureResult, ok as fok};
use futures::io;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::prelude::*;
use std::sync::Arc;

// Include the generated FIDL bindings for the `DeviceSetting` service.
use fidl_fuchsia_devicesettings::{
    DeviceSettingsManager,
    DeviceSettingsManagerImpl,
    DeviceSettingsManagerMarker,
    DeviceSettingsManagerRequest,
    DeviceSettingsManagerRequestStream,
    DeviceSettingsWatcherProxy,
    Status,
    ValueType
};

type Watchers = Arc<Mutex<HashMap<String, Vec<DeviceSettingsWatcherProxy>>>>;

struct DeviceSettingsManagerServer {
    setting_file_map: HashMap<String, String>,
    watchers: Watchers,
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

fn spawn_device_settings_server(state: DeviceSettingsManagerServer, chan: async::Channel) {
    let state = Arc::new(Mutex::new(state));
    async::spawn(DeviceSettingsManagerRequestStream::from_channel(chan)
        .for_each(move |req| {
            let state = state.clone();
            let mut state = state.lock();
            match req {
                DeviceSettingsManagerRequest::GetInteger { key, responder } => {
                    let file = if let Some(f) = state.setting_file_map.get(&key) {
                        f
                    } else {
                        return responder.send(0, Status::ErrInvalidSetting)
                            .into_future()
                    };
                    match read_file(file) {
                        Err(e) => {
                            if e.kind() == io::ErrorKind::NotFound {
                                responder.send(0, Status::ErrNotSet)
                            } else {
                                fx_log_err!("reading integer: {:?}", e);
                                responder.send(0, Status::ErrRead)
                            }
                        }
                        Ok(str) => match str.parse::<i64>() {
                            Err(_e) => responder.send(0, Status::ErrIncorrectType),
                            Ok(i) => responder.send(i, Status::Ok),
                        },
                    }
                }
                DeviceSettingsManagerRequest::GetString { key, responder } => {
                    let file = if let Some(f) = state.setting_file_map.get(&key) {
                        f
                    } else {
                        return responder.send("", Status::ErrInvalidSetting).into_future()
                    };
                    match read_file(file) {
                        Err(e) => {
                            if e.kind() == io::ErrorKind::NotFound {
                                responder.send("", Status::ErrNotSet)
                            } else {
                                fx_log_err!("reading string: {:?}", e);
                                responder.send("", Status::ErrRead)
                            }
                        }
                        Ok(s) => responder.send(&*s, Status::Ok),
                    }
                }
                DeviceSettingsManagerRequest::SetInteger { key, val, responder } => {
                    match state.set_key(&key, val.to_string().as_bytes(), ValueType::Number) {
                        Ok(r) => responder.send(r),
                        Err(e) => {
                            fx_log_err!("setting integer: {:?}", e);
                            responder.send(false)
                        }
                    }
                }
                DeviceSettingsManagerRequest::SetString { key, val, responder } => {
                    fx_log_info!("setting string key: {:?}, val: {:?}", key, val);
                    match state.set_key(&key, val.as_bytes(), ValueType::Text) {
                        Ok(mut r) => responder.send(r),
                        Err(e) => {
                            fx_log_err!("setting string: {:?}", e);
                            responder.send(false)
                        }
                    }
                }
                DeviceSettingsManagerRequest::Watch { key, watcher, responder } => {
                    if !state.setting_file_map.contains_key(&key) {
                        return responder.send(Status::ErrInvalidSetting).into_future();
                    }
                    match watcher.into_proxy() {
                        Err(e) => {
                            fx_log_err!("getting watcher proxy: {:?}", e);
                            responder.send(Status::ErrUnknown)
                        }
                        Ok(w) => {
                            let mut map = state.watchers.lock();
                            let mv = map.entry(key).or_insert(Vec::new());
                            mv.push(w);
                            responder.send(Status::Ok)
                        }
                    }
                }
            }.into_future()
        })
        .map(|_| ())
        .recover(|e| eprintln!("error running device settings server: {:?}", e))
    )
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
        .add_service((DeviceSettingsManagerMarker::NAME, move |channel| {
            let mut d = DeviceSettingsManagerServer {
                setting_file_map: HashMap::new(),
                watchers: watchers.clone(),
            };

            d.initialize_keys(DATA_DIR, &["DeviceName", "TestSetting",
                "Display.Brightness", "Audio", "FactoryReset"]);


            spawn_device_settings_server(d, channel)
        }))
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

    use fidl_fuchsia_devicesettings::DeviceSettingsManagerProxy;

    fn async_test<F, Fut>(keys: &[&str], f: F)
    where
        F: FnOnce(DeviceSettingsManagerProxy) -> Fut,
        Fut: Future<Item = (), Error = fidl::Error>,
    {
        let (mut exec, device_settings, _t) = setup(keys).expect("Setup should not have failed");

        let test_fut = f(device_settings);

        exec.run_singlethreaded(test_fut)
            .expect("executor run failed");
    }

    fn setup(keys: &[&str]) -> Result<(async::Executor, DeviceSettingsManagerProxy, TempDir), ()> {
        let exec = async::Executor::new().unwrap();
        let mut device_settings = DeviceSettingsManagerServer {
            setting_file_map: HashMap::new(),
            watchers: Arc::new(Mutex::new(HashMap::new())),
        };
        let tmp_dir = TempDir::new("ds_test").unwrap();

        device_settings.initialize_keys(tmp_dir.path().to_str().unwrap(), keys);

        let (server_chan, client_chan) = zx::Channel::create().unwrap();
        let server_chan = async::Channel::from_channel(server_chan).unwrap();
        let client_chan = async::Channel::from_channel(client_chan).unwrap();

        spawn_device_settings_server(device_settings, server_chan);

        let proxy = DeviceSettingsManagerProxy::new(client_chan);

        // return tmp_dir to keep it in scope
        return Ok((exec, proxy, tmp_dir));
    }

    #[test]
    fn test_int() {
        async_test(&["TestKey"], |device_settings| {
            device_settings
                .set_integer("TestKey", 18)
                .and_then(move |response| {
                    assert!(response, "set_integer failed");
                    device_settings.get_integer("TestKey")
                })
                .and_then(move |response| {
                    assert_eq!(response, (18, Status::Ok));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_string() {
        async_test(&["TestKey"], |device_settings| {
            device_settings
                .set_string("TestKey", "mystring")
                .and_then(move |response| {
                    assert!(response, "set_string failed");
                    device_settings.get_string("TestKey")
                })
                .and_then(move |response| {
                    assert_eq!(response, ("mystring".to_string(), Status::Ok));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_invalid_key() {
        async_test(&[], |device_settings| {
            device_settings
                .get_string("TestKey")
                .and_then(move |response| {
                    assert_eq!(response, ("".to_string(), Status::ErrInvalidSetting));
                    device_settings.get_integer("TestKey")
                })
                .and_then(move |response| {
                    assert_eq!(response, (0, Status::ErrInvalidSetting));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_incorrect_type() {
        async_test(&["TestKey"], |device_settings| {
            device_settings
                .set_string("TestKey", "mystring")
                .and_then(move |response| {
                    assert!(response, "set_string failed");
                    device_settings.get_integer("TestKey")
                })
                .and_then(move |response| {
                    assert_eq!(response, (0, Status::ErrIncorrectType));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_not_set_err() {
        async_test(&["TestKey"], |device_settings| {
            device_settings
                .get_integer("TestKey")
                .and_then(move |response| {
                    assert_eq!(response, (0, Status::ErrNotSet));
                    device_settings.get_string("TestKey")
                })
                .and_then(move |response| {
                    assert_eq!(response, ("".to_string(), Status::ErrNotSet));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_multiple_keys() {
        async_test(&["TestKey1", "TestKey2"], |device_settings| {
            device_settings
                .set_integer("TestKey1", 18)
                .and_then(move |response| {
                    assert!(response, "set_integer failed");
                    device_settings
                        .set_string("TestKey2", "mystring")
                        .map(move |response| (response, device_settings))
                })
                .and_then(|(response, device_settings)| {
                    assert!(response, "set_string failed");
                    device_settings
                        .get_integer("TestKey1")
                        .map(move |response| (response, device_settings))
                })
                .and_then(|(response, device_settings)| {
                    assert_eq!(response, (18, Status::Ok));
                    device_settings.get_string("TestKey2")
                })
                .and_then(|response| {
                    assert_eq!(response, ("mystring".to_string(), Status::Ok));
                    Ok(())
                })
        });
    }
}
