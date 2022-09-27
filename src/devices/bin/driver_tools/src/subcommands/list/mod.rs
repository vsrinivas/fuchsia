// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    crate::common::{self, Device},
    anyhow::{Context, Result},
    args::ListCommand,
    bind::debugger::debug_dump::dump_bind_rules,
    fidl_fuchsia_driver_development as fdd,
    futures::join,
    std::{collections::HashSet, fmt::Write as OtherWrite, io::Write, iter::FromIterator},
};

pub async fn list(
    cmd: ListCommand,
    writer: &mut impl Write,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
) -> Result<()> {
    let empty: [String; 0] = [];
    let driver_info = common::get_driver_info(&driver_development_proxy, &empty);

    let driver_info = if cmd.loaded {
        // Query devices and create a hash set of loaded drivers.
        let device_info = common::get_device_info(&driver_development_proxy, &empty);

        // Await the futures concurrently.
        let (driver_info, device_info) = join!(driver_info, device_info);

        let loaded_driver_set: HashSet<String> =
            HashSet::from_iter(device_info?.into_iter().filter_map(|device_info| {
                let device: Device = device_info.into();
                let key = match device {
                    Device::V1(ref info) => &info.0.bound_driver_libname,
                    Device::V2(ref info) => {
                        // DFv2 nodes do not have a bound driver libname so the
                        // bound driver URL is selected instead.
                        &info.0.bound_driver_url
                    }
                };
                match key {
                    Some(key) => Some(key.to_owned()),
                    None => None,
                }
            }));

        // Filter the driver list by the hash set.
        driver_info?
            .into_iter()
            .filter(|driver| {
                let mut loaded = false;
                if let Some(ref libname) = driver.libname {
                    if loaded_driver_set.contains(libname) {
                        loaded = true;
                    }
                }
                if let Some(ref url) = driver.url {
                    if loaded_driver_set.contains(url) {
                        loaded = true
                    }
                }
                loaded
            })
            .collect()
    } else {
        driver_info.await.context("Failed to get driver info")?
    };

    if cmd.verbose {
        for driver in driver_info {
            if let Some(name) = driver.name {
                writeln!(writer, "{0: <10}: {1}", "Name", name)
                    .context("Failed to write to writer")?;
            }
            if let Some(url) = driver.url {
                writeln!(writer, "{0: <10}: {1}", "URL", url)
                    .context("Failed to write to writer")?;
            }
            if let Some(libname) = driver.libname {
                writeln!(writer, "{0: <10}: {1}", "Driver", libname)
                    .context("Failed to write to writer")?;
            }
            if let Some(device_categories) = driver.device_categories {
                write!(writer, "Device Categories: [").context("Failed to write to writer")?;

                for (i, category_table) in device_categories.iter().enumerate() {
                    if let Some(category) = &category_table.category {
                        if let Some(subcategory) = &category_table.subcategory {
                            if !subcategory.is_empty() {
                                write!(writer, "{}::{}", category, subcategory)?;
                            } else {
                                write!(writer, "{}", category,)?;
                            }
                        } else {
                            write!(writer, "{}", category,).context("Failed to write to writer")?;
                        }
                    }

                    if i != device_categories.len() - 1 {
                        write!(writer, ", ").context("Failed to write to writer")?;
                    }
                }
                writeln!(writer, "]").context("Failed to write to writer")?;
            }
            if let Some(package_hash) = driver.package_hash {
                let mut merkle_root = String::with_capacity(package_hash.merkle_root.len() * 2);
                for byte in package_hash.merkle_root.iter() {
                    write!(merkle_root, "{:02x}", byte).context("Failed to write to string")?;
                }
                writeln!(writer, "{0: <10}: {1}", "Merkle Root", &merkle_root)
                    .context("Failed to write to writer")?;
            }
            match driver.bind_rules {
                Some(fdd::BindRulesBytecode::BytecodeV1(bytecode)) => {
                    writeln!(writer, "{0: <10}: {1}", "Bytecode Version", 1)
                        .context("Failed to write to writer")?;
                    writeln!(
                        writer,
                        "{0: <10}({1} bytes): {2:?}",
                        "Bytecode:",
                        bytecode.len(),
                        bytecode
                    )
                    .context("Failed to write to writer")?;
                }
                Some(fdd::BindRulesBytecode::BytecodeV2(bytecode)) => {
                    writeln!(writer, "{0: <10}: {1}", "Bytecode Version", 2)
                        .context("Failed to write to writer")?;
                    writeln!(writer, "{0: <10}({1} bytes): ", "Bytecode:", bytecode.len())
                        .context("Failed to write to writer")?;
                    match dump_bind_rules(bytecode.clone()) {
                        Ok(bytecode_dump) => writeln!(writer, "{}", bytecode_dump)
                            .context("Failed to write to writer")?,
                        Err(err) => {
                            writeln!(
                                writer,
                                "  Issue parsing bytecode \"{}\": {:?}",
                                err, bytecode
                            )
                            .context("Failed to write to writer")?;
                        }
                    }
                }
                _ => writeln!(writer, "{0: <10}: {1}", "Bytecode Version", "Unknown")
                    .context("Failed to write to writer")?,
            }
            writeln!(writer).context("Failed to write to writer")?;
        }
    } else {
        for driver in driver_info {
            if let Some(name) = driver.name {
                let libname_or_url = driver.libname.or(driver.url).unwrap_or("".to_string());
                writeln!(writer, "{:<20}: {}", name, libname_or_url)
                    .context("Failed to write to writer")?;
            } else {
                let url_or_libname = driver.url.or(driver.libname).unwrap_or("".to_string());
                writeln!(writer, "{}", url_or_libname).context("Failed to write to writer")?;
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        argh::FromArgs,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_driver_index as fdi, fidl_fuchsia_pkg as fpkg, fuchsia_async as fasync,
        futures::{
            future::{Future, FutureExt},
            stream::StreamExt,
        },
    };

    /// Invokes `list` with `cmd` and runs a mock driver development server that
    /// invokes `on_driver_development_request` whenever it receives a request.
    /// The output of `list` that is normally written to its `writer` parameter
    /// is returned.
    async fn test_list<F, Fut>(cmd: ListCommand, on_driver_development_request: F) -> Result<String>
    where
        F: Fn(fdd::DriverDevelopmentRequest) -> Fut + Send + Sync + 'static,
        Fut: Future<Output = Result<()>> + Send + Sync,
    {
        let (driver_development_proxy, mut driver_development_requests) =
            fidl::endpoints::create_proxy_and_stream::<fdd::DriverDevelopmentMarker>()
                .context("Failed to create FIDL proxy")?;

        // Run the command and mock driver development server.
        let mut writer = Vec::new();
        let request_handler_task = fasync::Task::spawn(async move {
            while let Some(res) = driver_development_requests.next().await {
                let request = res.context("Failed to get next request")?;
                on_driver_development_request(request).await.context("Failed to handle request")?;
            }
            anyhow::bail!("Driver development request stream unexpectedly closed");
        });
        futures::select! {
            res = request_handler_task.fuse() => {
                res?;
                anyhow::bail!("Request handler task unexpectedly finished");
            }
            res = list(cmd, &mut writer, driver_development_proxy).fuse() => res.context("List command failed")?,
        }

        String::from_utf8(writer).context("Failed to convert list output to a string")
    }

    async fn run_driver_info_iterator_server(
        mut driver_infos: Vec<fdd::DriverInfo>,
        iterator: ServerEnd<fdd::DriverInfoIteratorMarker>,
    ) -> Result<()> {
        let mut iterator =
            iterator.into_stream().context("Failed to convert iterator into a stream")?;
        while let Some(res) = iterator.next().await {
            let request = res.context("Failed to get request")?;
            match request {
                fdd::DriverInfoIteratorRequest::GetNext { responder } => {
                    responder
                        .send(
                            &mut driver_infos
                                .drain(..)
                                .collect::<Vec<fdd::DriverInfo>>()
                                .into_iter(),
                        )
                        .context("Failed to send driver infos to responder")?;
                }
            }
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_verbose() {
        let cmd = ListCommand::from_args(&["list"], &["--verbose"]).unwrap();

        let output = test_list(cmd, |request: fdd::DriverDevelopmentRequest| async move {
            match request {
                fdd::DriverDevelopmentRequest::GetDriverInfo {
                    driver_filter: _,
                    iterator,
                    control_handle: _,
                } => run_driver_info_iterator_server(
                    vec![fdd::DriverInfo {
                        libname: Some("foo.so".to_owned()),
                        name: Some("foo".to_owned()),
                        url: Some("fuchsia-pkg://fuchsia.com/foo-package#meta/foo.cm".to_owned()),
                        bind_rules: None,
                        package_type: Some(fdi::DriverPackageType::Base),
                        package_hash: Some(fpkg::BlobId {
                            merkle_root: [
                                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                            ],
                        }),
                        device_categories: Some(vec![
                            fdi::DeviceCategory {
                                category: Some("connectivity".to_string()),
                                subcategory: Some("ethernet".to_string()),
                                ..fdi::DeviceCategory::EMPTY
                            },
                            fdi::DeviceCategory {
                                category: Some("usb".to_string()),
                                subcategory: None,
                                ..fdi::DeviceCategory::EMPTY
                            },
                        ]),
                        ..fdd::DriverInfo::EMPTY
                    }],
                    iterator,
                )
                .await
                .context("Failed to run driver info iterator server")?,
                _ => {}
            }
            Ok(())
        })
        .await
        .unwrap();

        assert_eq!(
            output,
            r#"Name      : foo
URL       : fuchsia-pkg://fuchsia.com/foo-package#meta/foo.cm
Driver    : foo.so
Device Categories: [connectivity::ethernet, usb]
Merkle Root: 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20
Bytecode Version: Unknown

"#
        );
    }
}
