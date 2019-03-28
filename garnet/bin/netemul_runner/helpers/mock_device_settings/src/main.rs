// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{format_err, Error};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_devicesettings::{
    DeviceSettingsManagerMarker, DeviceSettingsManagerRequest, DeviceSettingsManagerRequestStream,
    DeviceSettingsWatcherProxy, Status, ValueType,
};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{future, TryFutureExt, TryStreamExt};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
#[structopt(name = "mock_device_settings")]
/// Serves DeviceSettingsManager with a memory-backed database
struct Opt {
    #[structopt(short = "s")]
    /// Starts with a pre-set string key, format [k]=[v]
    /// e.g. DeviceName=my-device-name
    string_key: Vec<String>,
    #[structopt(short = "i")]
    /// Starts with a pre-set integer key, format [k]=[v]
    /// e.g. Audio=100
    int_key: Vec<String>,
}

enum Key {
    StringKey(String),
    IntKey(i64),
}

struct DeviceSettingsManagerServer {
    keys: HashMap<String, Key>,
    watchers: HashMap<String, Vec<DeviceSettingsWatcherProxy>>,
}

impl DeviceSettingsManagerServer {
    fn run_watchers(&mut self, key: &str, t: ValueType) {
        if let Some(m) = self.watchers.get_mut(key) {
            m.retain(|w| {
                if let Err(e) = w.on_change_settings(t) {
                    match e {
                        fidl::Error::ClientRead(zx::Status::PEER_CLOSED)
                        | fidl::Error::ClientWrite(zx::Status::PEER_CLOSED) => {
                            return false;
                        }
                        _ => {}
                    };
                    println!("Error call watcher: {:?}", e);
                }
                return true;
            });
        }
    }
}

fn split_once(in_string: &str) -> Result<(&str, &str), Error> {
    let mut splitter = in_string.splitn(2, '=');
    let first = splitter.next().ok_or(format_err!("Invalid key value format"))?;
    let second = splitter.next().ok_or(format_err!("Invalid key value format"))?;
    Ok((first, second))
}

fn config_state(state: &mut DeviceSettingsManagerServer, opt: Opt) -> Result<(), Error> {
    for s_key in opt.string_key {
        let (k, v) = split_once(&s_key)?;
        println!("Startup {}={}", k, v);
        state.keys.insert(String::from(k), Key::StringKey(String::from(v)));
    }

    for i_key in opt.int_key {
        let (k, v) = split_once(&i_key)?;
        let v = v.parse::<i64>()?;
        println!("Startup {}={}", k, v);
        state.keys.insert(String::from(k), Key::IntKey(v));
    }

    Ok(())
}

fn spawn_device_settings_server(
    state: Arc<Mutex<DeviceSettingsManagerServer>>,
    chan: fasync::Channel,
) {
    fasync::spawn(
        DeviceSettingsManagerRequestStream::from_channel(chan)
            .try_for_each(move |req| {
                let mut state = state.lock().unwrap();
                future::ready(match req {
                    DeviceSettingsManagerRequest::GetInteger { key, responder } => {
                        match state.keys.get(&key) {
                            None => {
                                println!("Key {} doesn't exist", key);
                                responder.send(0, Status::ErrNotSet)
                            }
                            Some(Key::IntKey(val)) => responder.send(*val, Status::Ok),
                            _ => {
                                println!("Key {} is not an integer", key);
                                responder.send(0, Status::ErrIncorrectType)
                            }
                        }
                    }
                    DeviceSettingsManagerRequest::GetString { key, responder } => {
                        match state.keys.get(&key) {
                            None => {
                                println!("Key {} doesn't exist", key);
                                responder.send("", Status::ErrNotSet)
                            }
                            Some(Key::StringKey(val)) => responder.send(val, Status::Ok),
                            _ => {
                                println!("Key {} is not a string", key);
                                responder.send("", Status::ErrIncorrectType)
                            }
                        }
                    }
                    DeviceSettingsManagerRequest::SetInteger { key, val, responder } => {
                        match state.keys.get(&key) {
                            Some(Key::IntKey(_)) => {
                                println!("Set {}={}", key, val);
                                state.run_watchers(&key, ValueType::Number);
                                state.keys.insert(key, Key::IntKey(val));
                                responder.send(true)
                            }
                            _ => {
                                println!("Failed to set integer key {}={}", key, val);
                                responder.send(false)
                            }
                        }
                    }
                    DeviceSettingsManagerRequest::SetString { key, val, responder } => {
                        match state.keys.get(&key) {
                            Some(Key::StringKey(_)) => {
                                println!("Set {}={}", key, val);
                                state.run_watchers(&key, ValueType::Text);
                                state.keys.insert(key, Key::StringKey(val));
                                responder.send(true)
                            }
                            _ => {
                                println!("Failed to set string key {}={}", key, val);
                                responder.send(false)
                            }
                        }
                    }
                    DeviceSettingsManagerRequest::Watch { key, watcher, responder } => {
                        match state.keys.get(&key) {
                            None => {
                                println!("Can't watch key {}, it doesn't exist", key);
                                responder.send(Status::ErrInvalidSetting)
                            }
                            _ => match watcher.into_proxy() {
                                Ok(watcher) => {
                                    let mv = state.watchers.entry(key).or_insert(Vec::new());
                                    mv.push(watcher);
                                    responder.send(Status::Ok)
                                }
                                Err(e) => {
                                    println!("Error watching key {}: {}", key, e);
                                    responder.send(Status::ErrUnknown)
                                }
                            },
                        }
                    }
                })
            })
            .map_ok(|_| ())
            .unwrap_or_else(|e| eprintln!("error running mock device settings server: {:?}", e)),
    );
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut state = DeviceSettingsManagerServer { keys: HashMap::new(), watchers: HashMap::new() };
    let () = config_state(&mut state, opt)?;
    let state = Arc::new(Mutex::new(state));
    let services = ServicesServer::new()
        .add_service((DeviceSettingsManagerMarker::NAME, move |channel| {
            spawn_device_settings_server(state.clone(), channel)
        }))
        .start()
        .map_err(|e| e.context("error starting service server"))?;
    await!(services)
}

#[cfg(test)]
mod test {
    use failure::ResultExt;
    use fidl_fuchsia_devicesettings::{
        DeviceSettingsManagerMarker, DeviceSettingsWatcherMarker, DeviceSettingsWatcherRequest,
        Status,
    };
    use fuchsia_app::client;
    use fuchsia_async as fasync;
    use futures::{future, TryStreamExt};

    #[fasync::run_singlethreaded]
    #[test]
    async fn test_string_key() {
        let manager = client::connect_to_service::<DeviceSettingsManagerMarker>().unwrap();
        let res = await!(manager.set_string("StringKey", "HelloWorld"))
            .context("can't set string")
            .unwrap();
        assert_eq!(res, true);
        let (val, status) =
            await!(manager.get_string("StringKey")).context("can't get string").unwrap();
        assert_eq!(status, Status::Ok);
        assert_eq!(val, "HelloWorld");
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn test_int_key() {
        let manager = client::connect_to_service::<DeviceSettingsManagerMarker>().unwrap();
        let res = await!(manager.set_integer("IntKey", 1234)).context("can't set int").unwrap();
        assert_eq!(res, true);
        let (val, status) = await!(manager.get_integer("IntKey")).context("can't get int").unwrap();
        assert_eq!(status, Status::Ok);
        assert_eq!(val, 1234);
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn test_watch_key() {
        let manager = client::connect_to_service::<DeviceSettingsManagerMarker>().unwrap();
        let (watcher_client_end, watcher_server_end) =
            fidl::endpoints::create_endpoints::<DeviceSettingsWatcherMarker>().unwrap();
        let status = await!(manager.watch("WatchKey", watcher_client_end))
            .context("can't set watch")
            .unwrap();
        assert_eq!(status, Status::Ok);

        let res = await!(manager.set_integer("WatchKey", 3456)).context("can't set int").unwrap();
        assert_eq!(res, true);

        let (val, status) =
            await!(manager.get_integer("WatchKey")).context("can't get int").unwrap();
        assert_eq!(status, Status::Ok);
        assert_eq!(val, 3456);

        let mut wait_watch = watcher_server_end
            .into_stream()
            .context("Can't take request stream")
            .unwrap()
            .try_filter_map(|req| match req {
                DeviceSettingsWatcherRequest::OnChangeSettings { type_: _, control_handle: _ } => {
                    future::ok(Some(()))
                }
            });

        await!(wait_watch.try_next()).unwrap();
    }
}
