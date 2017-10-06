// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate bytes;
extern crate fidl;
extern crate fuchsia_zircon;
extern crate futures;
extern crate mxruntime;
extern crate mxruntime_sys;
extern crate tokio_core;
extern crate tokio_fuchsia;

use bytes::ByteOrder;
use futures::{Future, future};
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::prelude::*;
use std::io;
use std::os::unix::ffi::OsStrExt;
use tokio_core::reactor;

// Include the generated FIDL bindings for the `DeviceSetting` service.
extern crate garnet_public_lib_device_settings_fidl;
use garnet_public_lib_device_settings_fidl::{DeviceSettingsManager, Status};

type FidlImmediate<T> = future::FutureResult<T, fidl::CloseChannel>;

struct DeviceSettingsManagerServer {
    setting_file_map: HashMap<String, String>,
}

impl DeviceSettingsManagerServer {
    fn initialize_keys(&mut self, data_dir: String, keys: &[&str]) {
        self.setting_file_map = HashMap::with_capacity(keys.len());
        for k in keys {
            self.setting_file_map.insert(
                k.to_string(),
                format!(
                    "{}/{}",
                    data_dir,
                    k.to_lowercase()
                ),
            );
        }
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
    type GetInteger = FidlImmediate<(i64, Status)>;

    fn get_integer(&mut self, name: String) -> Self::GetInteger {
        let file = if let Some(f) = self.setting_file_map.get(&name) {
            f
        } else {
            return future::ok((0, Status::ErrInvalidSetting));
        };
        match read_file(file) {
            Err(e) => {
                if e.kind() == io::ErrorKind::NotFound {
                    return future::ok((0, Status::ErrNotSet));
                }
                eprintln!("DeviceSetting: Error reading integer: {:?}", e);
                return future::ok((0, Status::ErrRead));
            }
            Ok(str) => {
                match str.parse::<i64>() {
                    Err(_e) => future::ok((0, Status::ErrIncorrectType)),
                    Ok(i) => future::ok((i, Status::Ok)),
                }
            }
        }
    }

    type SetInteger = FidlImmediate<bool>;

    fn set_integer(&mut self, name: String, val: i64) -> Self::SetInteger {
        let file = if let Some(f) = self.setting_file_map.get(&name) {
            f
        } else {
            return future::ok(false);
        };
        if let Err(e) = write_to_file(file, val.to_string().as_bytes()) {
            eprintln!("DeviceSetting: Error setting integer: {:?}", e);
            return future::ok(false);
        }
        future::ok(true)
    }

    type GetString = FidlImmediate<(String, Status)>;

    fn get_string(&mut self, name: String) -> Self::GetString {
        let file = if let Some(f) = self.setting_file_map.get(&name) {
            f
        } else {
            return future::ok(("".to_string(), Status::ErrInvalidSetting));
        };
        match read_file(file) {
            Err(e) => {
                if e.kind() == io::ErrorKind::NotFound {
                    return future::ok(("".to_string(), Status::ErrNotSet));
                }
                eprintln!("DeviceSetting: Error reading string: {:?}", e);
                return future::ok(("".to_string(), Status::ErrRead));
            }
            Ok(s) => future::ok((s, Status::Ok)),
        }
    }

    type SetString = FidlImmediate<bool>;

    fn set_string(&mut self, name: String, val: String) -> Self::SetString {
        let file = if let Some(f) = self.setting_file_map.get(&name) {
            f
        } else {
            return future::ok(false);
        };
        if let Err(e) = write_to_file(file, val.as_bytes()) {
            eprintln!("DeviceSetting: Error setting string: {:?}", e);
            return future::ok(false);
        }
        future::ok(true)
    }
}

// TODO(anmittal): Use log crate and use that for logging
fn main() {
    let mut core = reactor::Core::new().expect("Unable to create core");
    let handle = core.handle();


    // TODO(raggi): clean this up, should check for handle_invalid etc.
    // We should update mxruntime to provide a safe version of this returning result or option.
    let fdio_handle = unsafe {
        mxruntime_sys::zx_get_startup_handle(mxruntime::HandleType::ServiceRequest as u32)
    };

    let fdio_channel =
        tokio_fuchsia::Channel::from_channel(
            fuchsia_zircon::Channel::from(unsafe { fuchsia_zircon::Handle::from_raw(fdio_handle) }),
            &handle,
        ).unwrap();

    let server = fdio_channel.repeat_server(0, |_chan, ref mut buf| {
        let fdio_op = bytes::LittleEndian::read_u32(&buf.bytes()[4..8]);

        let path = std::ffi::OsStr::from_bytes(&buf.bytes()[48..]).to_owned();

        if fdio_op != 259 {
            eprintln!(
                "service request channel received unknown op: {:?}",
                &fdio_op
            );
        }

        println!(
            "service request channel received open request for path: {:?}",
            &path
        );

        if buf.n_handles() != 1 {
            eprintln!(
                "service request channel received invalid handle count: {}",
                buf.n_handles()
            );

            // TODO(raggi): repeat_server should actually be doing this:
            for i in 0..buf.n_handles() {
                std::mem::drop(buf.take_handle(i).unwrap());
            }
            return;
        }

        let service_channel = fuchsia_zircon::Channel::from(buf.take_handle(0).unwrap());

        let mut d = DeviceSettingsManagerServer { setting_file_map: HashMap::new() };
        d.initialize_keys(DATA_DIR.to_owned(), &["Timezone"]);
        // Create data directory
        let _ = fs::create_dir_all(DATA_DIR);

        match fidl::Server::new(
            DeviceSettingsManager::Dispatcher(d),
            service_channel,
            &handle,
        ) {
            Ok(server) => {
                handle.spawn(server.map_err(move |e| match e {
                    fidl::Error::IoError(ie) => {
                        if ie.kind() != io::ErrorKind::ConnectionAborted {
                            eprintln!("runtime fidl server error for {:?}: {:?}", path, ie);
                        }
                    }
                    _ => {
                        eprintln!("runtime fidl server error for {:?}: {:?}", path, e);
                    }
                }))
            }
            Err(e) => eprintln!("service spawn for {:?} failed: {:?}", path, e),
        }
    });

    if let Err(e) = core.run(server) {
        if e.kind() != io::ErrorKind::ConnectionAborted {
            println!("Error running server: {:?}", e);
        }
    }
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
        let mut device_settings = DeviceSettingsManagerServer { setting_file_map: HashMap::new() };
        let tmp_dir = TempDir::new("ds_test").unwrap();


        device_settings.initialize_keys(tmp_dir.path().to_str().unwrap().to_string(), keys);

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
