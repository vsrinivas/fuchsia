// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]
use anyhow::{Context, Error};

// Include Brightness Control FIDL bindings
use argh::FromArgs;
use fidl_fuchsia_hardware_backlight::{
    DeviceMarker as BacklightMarker, DeviceProxy as BacklightProxy,
};
use fidl_fuchsia_ui_brightness::ControlMarker as BrightnessControlMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::fx_log_info;

const ASSUMED_MAX_BRIGHTNESS: f64 = 250.0;

fn open_backlight() -> Result<BacklightProxy, Error> {
    fx_log_info!("Opening backlight");
    let (proxy, server) = fidl::endpoints::create_proxy::<BacklightMarker>()
        .context("Failed to create backlight proxy")?;
    // TODO(kpt): Don't hardcode this path b/138666351
    fdio::service_connect("/dev/class/backlight/000", server.into_channel())
        .context("Failed to connect built-in service")?;
    Ok(proxy)
}

async fn get_max_brightness() -> Result<f32, Error> {
    let proxy = open_backlight()?;
    let connection_result = proxy.get_max_absolute_brightness().await;
    let max_brightness = connection_result.unwrap_or_else(|e| {
        fx_log_info!("Didn't connect correctly, got err {}", e);
        Ok(ASSUMED_MAX_BRIGHTNESS)
    });
    Ok(max_brightness.unwrap_or_else(|e| {
        fx_log_info!("Didn't get the max_brightness back, got err {}", e);
        ASSUMED_MAX_BRIGHTNESS
    }) as f32)
}

#[derive(FromArgs, PartialEq, Debug)]
/// Operation: Set or Watch.
struct Operation {
    #[argh(subcommand)]
    sub_command: MySubCommandEnum,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum MySubCommandEnum {
    Set(Set),
    Watch(Watch),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Set Operation.
#[argh(subcommand, name = "set")]
struct Set {
    #[argh(subcommand)]
    brightness: SetBrightness,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Watch Operation.
#[argh(subcommand, name = "watch")]
struct Watch {
    #[argh(subcommand)]
    brightness: WatchBrightness,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum WatchBrightness {
    AutoBrightness(AutoBrightness),
    CurrentBrightness(CurrentBrightness),
    MaxBrightness(MaxBrightness),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SetBrightness {
    AutoBrightness(AutoBrightness),
    ManualBrightness(ManualBrightness),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Set(Watch) auto brightness mode.
#[argh(subcommand, name = "auto-brightness")]
struct AutoBrightness {}

#[derive(FromArgs, PartialEq, Debug)]
/// Watch max brightness.
#[argh(subcommand, name = "max-brightness")]
struct MaxBrightness {}

#[derive(FromArgs, PartialEq, Debug)]
/// Watch current brightness, return 0-1 by default.
#[argh(subcommand, name = "current-brightness")]
struct CurrentBrightness {
    #[argh(switch, short = 'n')]
    /// using nits instead of 0-1.
    use_nits: bool,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Set manual brightness.
#[argh(subcommand, name = "manual-brightness")]
struct ManualBrightness {
    #[argh(option, short = 'v')]
    /// set manual to a certain number if value is present, using 0-1 by default.
    value: Option<f32>,
    #[argh(switch, short = 'n')]
    /// using nits instead of 0-1 if nits is present.
    use_nits: bool,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let brightness = connect_to_service::<BrightnessControlMarker>()
        .context("Failed to connect to brightness service")?;

    let operation: Operation = argh::from_env();
    let max_brightness = get_max_brightness().await.unwrap();
    match operation.sub_command {
        MySubCommandEnum::Set(set) => match set.brightness {
            SetBrightness::AutoBrightness(_auto_brightness) => {
                println!("Setting to auto brightness mode");
                brightness.set_auto_brightness()?;
            }
            SetBrightness::ManualBrightness(manual) => {
                if manual.value.is_none() {
                    let auto_on = brightness.watch_auto_brightness().await?;
                    if auto_on {
                        println!("Setting to manual brightness mode");
                        let current = brightness.watch_current_brightness().await?;
                        brightness.set_manual_brightness(current)?;
                        let auto = brightness.watch_auto_brightness().await?;
                        println!("{}", auto);
                    } else {
                        println!("Already in manual brightness.");
                    }
                } else {
                    if manual.use_nits {
                        let normalized = manual.value.unwrap() / max_brightness;
                        brightness.set_manual_brightness(normalized)?;
                        let current = brightness.watch_current_brightness().await?;
                        println!("{}", current);
                    } else {
                        brightness.set_manual_brightness(manual.value.unwrap())?;
                        let current = brightness.watch_current_brightness().await?;
                        println!("{}", current);
                    }
                }
            }
        },
        MySubCommandEnum::Watch(watch) => match watch.brightness {
            WatchBrightness::AutoBrightness(_auto_brightness) => {
                let current = brightness.watch_auto_brightness().await?;
                println!("{:?}", current);
            }
            WatchBrightness::CurrentBrightness(current_brightness) => {
                let mut current = brightness.watch_current_brightness().await?;
                if current_brightness.use_nits {
                    current = max_brightness * current;
                    println!("{:?}", current);
                } else {
                    println!("{:?}", current);
                }
            }
            WatchBrightness::MaxBrightness(_max_brightness) => {
                println!("{:?}", max_brightness);
            }
        },
    }
    Ok(())
}

#[cfg(test)]

mod tests {
    use super::*;

    use fidl_fuchsia_hardware_backlight::{DeviceMarker, DeviceRequest};
    use fidl_fuchsia_ui_brightness::{ControlRequest, ControlRequestStream};
    use fuchsia_component::client::launcher;
    use fuchsia_component::{
        client::AppBuilder,
        server::{NestedEnvironment, ServiceFs},
    };
    use futures::lock::Mutex;
    use futures::{StreamExt, TryStreamExt};
    use std::str::from_utf8;
    use std::sync::Arc;
    use {
        directory_broker,
        fidl::endpoints::{create_endpoints, ServerEnd},
        fidl_fuchsia_io as fio,
        fuchsia_vfs_pseudo_fs_mt::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path,
            pseudo_directory,
        },
        fuchsia_zircon::HandleBased,
    };

    const BRIGHTNESS_TEST_PKG_URL: &str =
        "fuchsia-pkg://fuchsia.com/brightness_command_tests#meta/brightness_command.cmx";

    enum IncomingServices {
        BrightnessControl(ControlRequestStream),
    }

    fn run_test_services(
        auto_brightness: Arc<Mutex<bool>>,
        current_brightness: Arc<Mutex<f32>>,
    ) -> Result<NestedEnvironment, Error> {
        let mut fs = ServiceFs::new();
        fs.add_fidl_service(IncomingServices::BrightnessControl);

        let env = fs.create_salted_nested_environment("brightness_env");
        let auto_brightness = auto_brightness.clone();
        let current_brightness = current_brightness.clone();
        fasync::Task::spawn(fs.for_each_concurrent(None, move |req| {
            let auto_brightness = auto_brightness.clone();
            let current_brightness = current_brightness.clone();
            async move {
                let auto_brightness = auto_brightness.clone();
                let current_brightness = current_brightness.clone();
                match req {
                    IncomingServices::BrightnessControl(stream) => {
                        stream
                            .err_into::<Error>()
                            .try_for_each(|request| {
                                let auto_brightness = auto_brightness.clone();
                                let current_brightness = current_brightness.clone();
                                async move {
                                    let auto_brightness = auto_brightness.clone();
                                    let current_brightness = current_brightness.clone();
                                    match request {
                                        ControlRequest::WatchAutoBrightness { responder } => {
                                            let auto_brightness = auto_brightness.lock().await;
                                            responder.send(*auto_brightness)?;
                                        }
                                        ControlRequest::WatchCurrentBrightness { responder } => {
                                            let current_brightness =
                                                current_brightness.lock().await;
                                            responder.send(*current_brightness)?;
                                        }
                                        ControlRequest::SetAutoBrightness { control_handle: _ } => {
                                            let mut auto_brightness = auto_brightness.lock().await;
                                            *auto_brightness = true;
                                        }
                                        ControlRequest::SetManualBrightness {
                                            value,
                                            control_handle: _,
                                        } => {
                                            let mut current_brightness =
                                                current_brightness.lock().await;
                                            *current_brightness = value;
                                            let mut auto_brightness = auto_brightness.lock().await;
                                            *auto_brightness = false;
                                        }
                                        _ => panic!("Not expected request"),
                                    }
                                    Ok(())
                                }
                            })
                            .await
                            .unwrap();
                    }
                }
            }
        }))
        .detach();
        env
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_watch_auto_brightness() -> Result<(), Error> {
        let auto_brightness = Arc::new(Mutex::new(false));
        let current_brightness = Arc::new(Mutex::new(0.6));
        let env = run_test_services(auto_brightness.clone(), current_brightness.clone())?;

        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("watch")
            .arg("auto-brightness")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!("false\n", from_utf8(&output.stdout).unwrap());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_watch_current_brightness() -> Result<(), Error> {
        let auto_brightness = Arc::new(Mutex::new(true));
        let current_brightness = Arc::new(Mutex::new(0.6));
        let env = run_test_services(auto_brightness.clone(), current_brightness.clone())?;

        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("watch")
            .arg("current-brightness")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!("0.6\n", from_utf8(&output.stdout).unwrap());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_auto_brightness() -> Result<(), Error> {
        let auto_brightness = Arc::new(Mutex::new(false));
        let current_brightness = Arc::new(Mutex::new(0.6));
        let env = run_test_services(auto_brightness.clone(), current_brightness.clone())?;

        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("set")
            .arg("auto-brightness")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!("Setting to auto brightness mode\n", from_utf8(&output.stdout).unwrap());
        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("watch")
            .arg("auto-brightness")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!("true\n", from_utf8(&output.stdout).unwrap());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_manual_brightness_without_value_auto_on() -> Result<(), Error> {
        let auto_brightness = Arc::new(Mutex::new(true));
        let current_brightness = Arc::new(Mutex::new(0.6));
        let env = run_test_services(auto_brightness.clone(), current_brightness.clone())?;

        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("set")
            .arg("manual-brightness")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!(
            "Setting to manual brightness mode\nfalse\n",
            from_utf8(&output.stdout).unwrap()
        );
        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("watch")
            .arg("current-brightness")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!("0.6\n", from_utf8(&output.stdout).unwrap());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_manual_brightness_without_value_auto_off() -> Result<(), Error> {
        let auto_brightness = Arc::new(Mutex::new(false));
        let current_brightness = Arc::new(Mutex::new(0.6));
        let env = run_test_services(auto_brightness.clone(), current_brightness.clone())?;

        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("set")
            .arg("manual-brightness")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!("Already in manual brightness.\n", from_utf8(&output.stdout).unwrap());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_manual_brightness_with_value() -> Result<(), Error> {
        let auto_brightness = Arc::new(Mutex::new(true));
        let current_brightness = Arc::new(Mutex::new(0.6));
        let env = run_test_services(auto_brightness.clone(), current_brightness.clone())?;

        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("set")
            .arg("manual-brightness")
            .arg("-v")
            .arg("0.1")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!("0.1\n", from_utf8(&output.stdout).unwrap());
        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .arg("watch")
            .arg("auto-brightness")
            .output(&env.launcher())
            .unwrap()
            .await
            .unwrap();
        assert_eq!("false\n", from_utf8(&output.stdout).unwrap());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_watch_max_brightness() -> Result<(), Error> {
        let (client_end, server_end) = create_endpoints::<fio::NodeMarker>()?;
        fasync::Task::spawn(async move {
            let fake_dir = pseudo_directory! {
            "000" => directory_broker::DirectoryBroker::new(Box::new(|flags, mode, path, server_end| {
                    let (requests, control_handle) = ServerEnd::<DeviceMarker>::new(server_end.into_channel()).into_stream_and_control_handle().unwrap();

                    fasync::Task::spawn(async move {
                        requests
                            .err_into::<Error>()
                            .try_for_each(|request| {
                                async move {
                                    match request {
                                            DeviceRequest::GetMaxAbsoluteBrightness { responder } => {
                                                responder.send(&mut Ok(ASSUMED_MAX_BRIGHTNESS))?;
                                            }

                                            _ => panic!("Not expected request"),
                                        };
                                    Ok(())
                                }
                            })
                            .await
                            .unwrap();
                    }).detach();
                }))
                };

            fake_dir.open(
                ExecutionScope::new(),
                fio::OPEN_RIGHT_READABLE,
                fio::MODE_TYPE_DIRECTORY,
                path::Path::empty(),
                server_end,
            );
        }).detach();
        let output = AppBuilder::new(BRIGHTNESS_TEST_PKG_URL)
            .add_handle_to_namespace(
                "/dev/class/backlight".to_string(),
                client_end.into_channel().into_handle(),
            )
            .arg("watch")
            .arg("max-brightness")
            .output(&launcher()?)?
            .await
            .unwrap();
        assert_eq!(format!("{0:.1}\n", ASSUMED_MAX_BRIGHTNESS), from_utf8(&output.stdout).unwrap());

        Ok(())
    }
}
