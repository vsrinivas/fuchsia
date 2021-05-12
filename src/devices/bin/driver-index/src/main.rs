// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Context};
use bind::encode_bind_program_v1 as bind_v1;
use bind::instruction::DeviceProperty;
use fidl_fuchsia_driver_framework as fdf;
use fidl_fuchsia_driver_framework::{DriverIndexRequest, DriverIndexRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon::{zx_status_t, Status};
use futures::prelude::*;
use serde::Deserialize;
use std::collections::HashSet;
use std::rc::Rc;

fn encode_err_to_stdio_err(error: bind::compiler::BindProgramDecodeError) -> std::io::Error {
    std::io::Error::new(std::io::ErrorKind::Other, format!("Error decoding bind file: {}", error))
}

#[derive(Deserialize)]
struct JsonDriver {
    bind_file: String,
    driver_url: String,
}

impl JsonDriver {
    fn to_driver(self) -> std::io::Result<Driver> {
        let bytes = std::fs::read(&self.bind_file)?;
        Driver::create(self.driver_url, bytes).map_err(encode_err_to_stdio_err)
    }
}

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    DriverIndexProtocol(DriverIndexRequestStream),
}

struct Driver {
    bind_program: Vec<bind_v1::RawInstruction<[u32; 3]>>,
    url: String,
}

impl Driver {
    fn matches(
        &self,
        properties: &Vec<DeviceProperty>,
    ) -> Result<Option<HashSet<DeviceProperty>>, bind::debugger::DebuggerError> {
        bind::debugger::debug(&self.bind_program, properties)
    }

    fn create(
        url: String,
        bind_program: Vec<u8>,
    ) -> Result<Driver, bind::compiler::BindProgramDecodeError> {
        Ok(Driver { bind_program: bind_v1::decode_from_bytecode_v1(&bind_program)?, url: url })
    }
}

impl std::fmt::Display for Driver {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", &self.url)
    }
}

struct Indexer {
    drivers: Vec<Driver>,
}

impl Indexer {
    fn new(drivers: Vec<JsonDriver>) -> Result<Indexer, anyhow::Error> {
        let drivers: Result<Vec<_>, _> = drivers.into_iter().map(|d| d.to_driver()).collect();
        Ok(Indexer { drivers: drivers? })
    }

    #[allow(dead_code)]
    fn add_driver(&mut self, driver: Driver) {
        self.drivers.push(driver);
    }

    fn match_driver(&self, args: fdf::NodeAddArgs) -> fdf::DriverIndexMatchDriverResult {
        if args.properties.is_none() {
            return Err(Status::INVALID_ARGS.into_raw());
        }
        let properties = args.properties.unwrap();
        let properties = node_to_device_property(&properties)?;
        for driver in &self.drivers {
            match driver.matches(&properties) {
                Ok(matched_properties) => {
                    if matched_properties.is_some() {
                        return Ok(fdf::MatchedDriver {
                            url: Some(driver.url.clone()),
                            ..fdf::MatchedDriver::EMPTY
                        });
                    }
                    continue;
                }
                Err(e) => {
                    // If a driver has a bind error we will keep trying to match the other drivers
                    // instead of returning an error.
                    eprintln!("Driver {}: bind error: {}", driver, e);
                    continue;
                }
            }
        }
        Err(Status::NOT_FOUND.into_raw())
    }
}

fn node_to_device_property(
    node_properties: &Vec<fdf::NodeProperty>,
) -> Result<Vec<DeviceProperty>, zx_status_t> {
    let mut device_properties = Vec::<DeviceProperty>::with_capacity(node_properties.len());

    for property in node_properties {
        if property.key.is_none() || property.value.is_none() {
            return Err(Status::INVALID_ARGS.into_raw());
        }
        device_properties
            .push(DeviceProperty { key: property.key.unwrap(), value: property.value.unwrap() });
    }
    Ok(device_properties)
}

async fn run_index_server(
    indexer: Rc<Indexer>,
    stream: DriverIndexRequestStream,
) -> Result<(), anyhow::Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            let indexer = indexer.clone();
            match request {
                DriverIndexRequest::MatchDriver { args, responder } => {
                    responder
                        .send(&mut indexer.match_driver(args))
                        .context("error sending response")?;
                }
            }
            Ok(())
        })
        .await?;
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    let data = std::fs::read_to_string("/pkg/config/driver-index.json")?;
    let drivers: Vec<JsonDriver> = serde_json::from_str(&data)?;
    let index = Rc::new(Indexer::new(drivers)?);

    service_fs.dir("svc").add_fidl_service(IncomingRequest::DriverIndexProtocol);

    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async {
            // match on `request` and handle each protocol.
            match request {
                IncomingRequest::DriverIndexProtocol(stream) => {
                    run_index_server(index.clone(), stream).await
                }
            }
            .unwrap_or_else(|e| println!("{:?}", e))
        })
        .await;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn match_driver_empty_indexer() {
        let index = Rc::new(Indexer { drivers: vec![] });

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();

        let (a, _) = future::join(run_index_server(index.clone(), stream), async move {
            let property = fdf::NodeProperty {
                key: Some(0x10),
                value: Some(0x20),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap();
            assert_eq!(result, Err(Status::NOT_FOUND.into_raw()));
        })
        .await;
        a.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn match_driver_no_node_properties() {
        let index = Rc::new(Indexer {
            drivers: vec![Driver { bind_program: vec![], url: "my-url.cmx".to_string() }],
        });

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();

        let (a, _) = future::join(run_index_server(index.clone(), stream), async move {
            let args = fdf::NodeAddArgs::EMPTY;
            let result = proxy.match_driver(args).await.unwrap();
            assert_eq!(result, Err(Status::INVALID_ARGS.into_raw()));
        })
        .await;
        a.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn match_driver_bind_error() {
        let mut index = Rc::new(Indexer { drivers: vec![] });

        let property =
            fdf::NodeProperty { key: Some(0x10), value: Some(0x20), ..fdf::NodeProperty::EMPTY };

        let url = "my-url.cmx";
        let mut_index = Rc::get_mut(&mut index).unwrap();
        // Setting an empty bind program should give us an error.
        mut_index.add_driver(Driver { bind_program: vec![], url: url.to_string() });

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();
        let (a, _) = future::join(run_index_server(index.clone(), stream), async move {
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap();
            assert_eq!(result, Err(Status::NOT_FOUND.into_raw()));
        })
        .await;
        a.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn match_driver_success() {
        let mut index = Rc::new(Indexer { drivers: vec![] });

        let property =
            fdf::NodeProperty { key: Some(0x10), value: Some(0x20), ..fdf::NodeProperty::EMPTY };

        let key = bind::compiler::Symbol::NumberValue(property.key.unwrap() as u64);
        let value = bind::compiler::Symbol::NumberValue(property.value.unwrap() as u64);
        let instruction =
            bind::instruction::Instruction::Match(bind::instruction::Condition::Equal(key, value));
        let instruction = bind::instruction::InstructionInfo::new(instruction);
        let instruction = bind_v1::encode_instruction(instruction).unwrap();

        let url = "my-url.cmx";
        let mut_index = Rc::get_mut(&mut index).unwrap();
        mut_index.add_driver(Driver { bind_program: vec![instruction], url: url.to_string() });

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();
        let (a, _) = future::join(run_index_server(index.clone(), stream), async move {
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap();
            let received_url = result.unwrap().url.unwrap();
            assert_eq!(url.to_string(), received_url);
        })
        .await;
        a.unwrap();
    }

    // This test depends on '/pkg/config/drivers_for_test.json' existing in the test package.
    // The test reads that json file to determine which bind programs to read and index.
    #[fasync::run_singlethreaded(test)]
    async fn read_from_json() {
        let mut index = Indexer { drivers: vec![] };

        let data = std::fs::read_to_string("/pkg/config/drivers_for_test.json").unwrap();
        let drivers: Vec<JsonDriver> = serde_json::from_str(&data).unwrap();
        for driver in drivers {
            index.add_driver(driver.to_driver().unwrap());
        }

        let index = Rc::new(index);
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();

        let (server_result, _) =
            future::join(run_index_server(index.clone(), stream), async move {
                // Check the value from the 'test-bind' binary. This should match my-driver.cm
                let property = fdf::NodeProperty {
                    key: Some(bind::ddk_bind_constants::BIND_PROTOCOL),
                    value: Some(1),
                    ..fdf::NodeProperty::EMPTY
                };
                let args = fdf::NodeAddArgs {
                    properties: Some(vec![property]),
                    ..fdf::NodeAddArgs::EMPTY
                };
                let result = proxy.match_driver(args).await.unwrap();
                let received_url = result.unwrap().url.unwrap();
                assert_eq!(
                    "fuchsia-pkg://my-test-driver#meta/my-driver.cm".to_string(),
                    received_url
                );

                // Check the value from the 'test-bind2' binary. This should match my-driver2.cm
                let property = fdf::NodeProperty {
                    key: Some(bind::ddk_bind_constants::BIND_PROTOCOL),
                    value: Some(2),
                    ..fdf::NodeProperty::EMPTY
                };
                let args = fdf::NodeAddArgs {
                    properties: Some(vec![property]),
                    ..fdf::NodeAddArgs::EMPTY
                };
                let result = proxy.match_driver(args).await.unwrap();
                let received_url = result.unwrap().url.unwrap();
                assert_eq!(
                    "fuchsia-pkg://my-test-driver#meta/my-driver2.cm".to_string(),
                    received_url
                );

                // Check an unknown value. This should return the NOT_FOUND error.
                let property = fdf::NodeProperty {
                    key: Some(bind::ddk_bind_constants::BIND_PROTOCOL),
                    value: Some(3),
                    ..fdf::NodeProperty::EMPTY
                };
                let args = fdf::NodeAddArgs {
                    properties: Some(vec![property]),
                    ..fdf::NodeAddArgs::EMPTY
                };
                let result = proxy.match_driver(args).await.unwrap();
                assert_eq!(result, Err(Status::NOT_FOUND.into_raw()));
            })
            .await;
        server_result.unwrap();
    }
}
