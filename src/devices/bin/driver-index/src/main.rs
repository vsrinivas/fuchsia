// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Context};
use bind::interpreter::decode_bind_rules::DecodedBindRules;
use bind::interpreter::match_bind::{match_bind, DeviceProperties, MatchBindData, PropertyKey};
use cm_rust::FidlIntoNative;
use fidl_fuchsia_driver_development as fdd;
use fidl_fuchsia_driver_framework as fdf;
use fidl_fuchsia_driver_framework::{DriverIndexRequest, DriverIndexRequestStream};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_sys2 as fsys;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon::{zx_status_t, Status};
use futures::prelude::*;
use serde::Deserialize;
use std::cell::RefCell;
use std::ops::Deref;
use std::ops::DerefMut;
use std::rc::Rc;

mod package_resolver;

#[derive(Deserialize)]
struct JsonDriver {
    driver_url: String,
}

fn ignore_peer_closed(err: fidl::Error) -> Result<(), fidl::Error> {
    if err.is_closed() {
        Ok(())
    } else {
        Err(err)
    }
}

fn log_error(err: anyhow::Error) -> anyhow::Error {
    log::error!("{:#?}", err);
    err
}

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    DriverIndexProtocol(DriverIndexRequestStream),
    DriverDevelopmentProtocol(fdd::DriverIndexRequestStream),
}

fn get_rules_string_value(component: &cm_rust::ComponentDecl, key: &str) -> Option<String> {
    for entry in component.program.as_ref()?.info.entries.as_ref()? {
        if entry.key == key {
            match entry.value.as_ref()?.as_ref() {
                fidl_fuchsia_data::DictionaryValue::Str(s) => {
                    return Some(s.to_string());
                }
                fidl_fuchsia_data::DictionaryValue::StrVec(_) => {
                    return None;
                }
            }
        }
    }
    return None;
}

fn node_to_device_property(
    node_properties: &Vec<fdf::NodeProperty>,
) -> Result<DeviceProperties, zx_status_t> {
    let mut device_properties = DeviceProperties::new();

    for property in node_properties {
        if property.key.is_none() || property.value.is_none() {
            return Err(Status::INVALID_ARGS.into_raw());
        }
        let key = match property.key.as_ref().unwrap() {
            fdf::NodePropertyKey::IntValue(i) => PropertyKey::NumberKey(i.clone().into()),
            fdf::NodePropertyKey::StringValue(s) => PropertyKey::StringKey(s.clone()),
        };
        let value = match property.value.as_ref().unwrap() {
            fdf::NodePropertyValue::IntValue(i) => {
                bind::compiler::Symbol::NumberValue(i.clone().into())
            }
            fdf::NodePropertyValue::StringValue(s) => {
                bind::compiler::Symbol::StringValue(s.clone())
            }
            fdf::NodePropertyValue::BoolValue(b) => bind::compiler::Symbol::BoolValue(b.clone()),
        };
        device_properties.insert(key, value);
    }
    Ok(device_properties)
}

// Load the `component_url` driver out of `dir` which should be the root directory
// of that component. Will return Ok(None) if `component_url` is a valid component
// but it's not a driver component.
async fn load_driver(
    dir: &fio::DirectoryProxy,
    component_url: url::Url,
) -> Result<Option<ResolvedDriver>, anyhow::Error> {
    let component = io_util::open_file(
        &dir,
        std::path::Path::new(
            component_url
                .fragment()
                .ok_or(anyhow::anyhow!("{}: URL is missing fragment", component_url.as_str()))?,
        ),
        fio::OPEN_RIGHT_READABLE,
    )?;
    let component: fsys::ComponentDecl = io_util::read_file_fidl(&component)
        .await
        .with_context(|| format!("{}: Failed to read component", component_url.as_str()))?;
    let component = component.fidl_into_native();

    let runner = match component.get_runner() {
        Some(r) => r,
        None => return Ok(None),
    };
    if runner.str() != "driver" {
        return Ok(None);
    }

    let bind_path = get_rules_string_value(&component, "bind")
        .ok_or(anyhow::anyhow!("{}: Missing bind path", component_url.as_str()))?;
    let bind = io_util::open_file(&dir, std::path::Path::new(&bind_path), fio::OPEN_RIGHT_READABLE)
        .with_context(|| format!("{}: Failed to open bind", component_url.as_str()))?;
    let bind = io_util::read_file_bytes(&bind)
        .await
        .with_context(|| format!("{}: Failed to read bind", component_url.as_str()))?;
    let bind = DecodedBindRules::new(bind)
        .with_context(|| format!("{}: Failed to parse bind", component_url.as_str()))?;

    let driver_path = get_rules_string_value(&component, "binary")
        .ok_or(anyhow::anyhow!("{}: Missing binary path", component_url.as_str()))?;

    Ok(Some(ResolvedDriver {
        component_url: component_url,
        driver_path: driver_path,
        bind_rules: bind,
    }))
}

async fn load_boot_drivers(dir: fio::DirectoryProxy) -> Result<Vec<ResolvedDriver>, anyhow::Error> {
    let meta =
        io_util::open_directory(&dir, std::path::Path::new("meta"), fio::OPEN_RIGHT_READABLE)
            .context("boot: Failed to open meta")?;

    let entries = files_async::readdir(&meta).await.context("boot: failed to read meta")?;

    let mut drivers = std::vec::Vec::new();
    for entry in entries
        .iter()
        .filter(|entry| entry.kind == files_async::DirentKind::File)
        .filter(|entry| entry.name.ends_with(".cm"))
    {
        let url_string = "fuchsia-boot:///#meta/".to_string() + &entry.name;
        let url = url::Url::parse(&url_string)?;
        let driver = load_driver(&dir, url).await;
        if let Err(e) = driver {
            log::error!("Failed to load boot driver: {}: {}", url_string, e);
            continue;
        }
        if let Ok(Some(driver)) = driver {
            log::info!("Found boot driver: {}", driver.component_url.to_string());
            drivers.push(driver);
        }
    }
    Ok(drivers)
}

struct ResolvedDriver {
    component_url: url::Url,
    driver_path: String,
    bind_rules: DecodedBindRules,
}

impl std::fmt::Display for ResolvedDriver {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", &self.component_url)
    }
}

impl ResolvedDriver {
    async fn resolve(
        component_url: url::Url,
        resolver: &fidl_fuchsia_pkg::PackageResolverProxy,
    ) -> Result<ResolvedDriver, anyhow::Error> {
        let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
        let mut base_url = component_url.clone();
        base_url.set_fragment(None);

        let res = resolver.resolve(&base_url.as_str(), &mut std::iter::empty(), dir_server_end);
        res.await?.map_err(|e| {
            anyhow::anyhow!("{}: Failed to resolve package: {:?}", component_url.as_str(), e)
        })?;

        let driver = load_driver(&dir, component_url.clone()).await?;

        return driver.ok_or(anyhow::anyhow!(
            "{}: Component was not a driver-component",
            component_url.as_str()
        ));
    }

    fn matches(
        &self,
        properties: &DeviceProperties,
    ) -> Result<bool, bind::interpreter::common::BytecodeError> {
        match_bind(
            MatchBindData {
                symbol_table: &self.bind_rules.symbol_table,
                instructions: &self.bind_rules.instructions,
            },
            properties,
        )
        .map_err(|e| {
            log::error!("Driver {}: bind error: {}", self, e);
            e
        })
    }

    fn create_matched_driver(&self) -> fdf::MatchedDriver {
        let mut driver_url = self.component_url.clone();
        driver_url.set_fragment(Some(&self.driver_path));

        fdf::MatchedDriver {
            url: Some(self.component_url.as_str().to_string()),
            driver_url: Some(driver_url.as_str().to_string()),
            ..fdf::MatchedDriver::EMPTY
        }
    }

    fn create_driver_info(&self) -> fdd::DriverInfo {
        let mut driver_url = self.component_url.clone();
        driver_url.set_fragment(Some(&self.driver_path));
        fdd::DriverInfo {
            url: Some(driver_url.to_string()),
            bind_rules: Some(fdd::BindRulesBytecode::BytecodeV2(
                self.bind_rules.instructions.clone(),
            )),
            ..fdd::DriverInfo::EMPTY
        }
    }
}

enum BaseRepo {
    // We know that Base won't update so we can store these as resolved.
    Resolved(Vec<ResolvedDriver>),
    // If it's not resolved we store the clients waiting for it.
    NotResolved(Vec<fdf::DriverIndexWaitForBaseDriversResponder>),
}

struct Indexer {
    boot_repo: Vec<ResolvedDriver>,

    // base_repo needs to be in a RefCell because it starts out NotResolved,
    // but will eventually resolve when base packages are available.
    base_repo: RefCell<BaseRepo>,
}

impl Indexer {
    fn new(boot_repo: Vec<ResolvedDriver>, base_repo: BaseRepo) -> Indexer {
        Indexer { boot_repo: boot_repo, base_repo: RefCell::new(base_repo) }
    }

    fn load_base_repo(&self, base_repo: BaseRepo) {
        if let BaseRepo::NotResolved(waiters) = self.base_repo.borrow_mut().deref_mut() {
            while let Some(waiter) = waiters.pop() {
                match waiter.send().or_else(ignore_peer_closed) {
                    Err(e) => log::error!("Error sending to base_waiter: {:?}", e),
                    Ok(_) => continue,
                }
            }
        }
        self.base_repo.replace(base_repo);
    }

    fn match_driver(&self, args: fdf::NodeAddArgs) -> fdf::DriverIndexMatchDriverResult {
        if args.properties.is_none() {
            return Err(Status::INVALID_ARGS.into_raw());
        }
        let properties = args.properties.unwrap();
        let properties = node_to_device_property(&properties)?;

        for driver in &self.boot_repo {
            if Ok(true) == driver.matches(&properties) {
                return Ok(driver.create_matched_driver());
            }
        }

        let base_repo = self.base_repo.borrow();
        let base_drivers = match base_repo.deref() {
            BaseRepo::Resolved(drivers) => drivers,
            BaseRepo::NotResolved(_) => {
                return Err(Status::NOT_FOUND.into_raw());
            }
        };
        for driver in base_drivers {
            if Ok(true) == driver.matches(&properties) {
                return Ok(driver.create_matched_driver());
            }
        }
        Err(Status::NOT_FOUND.into_raw())
    }

    fn match_drivers_v1(&self, args: fdf::NodeAddArgs) -> fdf::DriverIndexMatchDriversV1Result {
        if args.properties.is_none() {
            return Err(Status::INVALID_ARGS.into_raw());
        }
        let properties = args.properties.unwrap();
        let properties = node_to_device_property(&properties)?;

        let base_repo = self.base_repo.borrow();
        let base_repo_iter = match base_repo.deref() {
            BaseRepo::Resolved(drivers) => drivers.iter(),
            BaseRepo::NotResolved(_) => [].iter(),
        };

        Ok(self
            .boot_repo
            .iter()
            .chain(base_repo_iter)
            .filter(|driver| driver.matches(&properties).unwrap_or(false))
            .map(|driver| driver.create_matched_driver())
            .collect())
    }
    fn get_driver_info(&self, driver_filter: Vec<String>) -> fdd::DriverIndexGetDriverInfoResult {
        let mut driver_info = Vec::new();

        for driver in &self.boot_repo {
            if driver_filter.len() == 0
                || driver_filter.iter().any(|f| f == driver.component_url.as_str())
            {
                driver_info.push(driver.create_driver_info());
            }
        }

        let base_repo = self.base_repo.borrow();
        if let BaseRepo::Resolved(drivers) = &base_repo.deref() {
            for driver in drivers {
                if driver_filter.len() == 0
                    || driver_filter.iter().any(|f| f == driver.component_url.as_str())
                {
                    driver_info.push(driver.create_driver_info());
                }
            }
        }

        Ok(driver_info)
    }
}

async fn run_driver_development_server(
    indexer: Rc<Indexer>,
    stream: fdd::DriverIndexRequestStream,
) -> Result<(), anyhow::Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            let indexer = indexer.clone();
            match request {
                fdd::DriverIndexRequest::GetDriverInfo { driver_filter, responder } => {
                    responder
                        .send(&mut indexer.get_driver_info(driver_filter))
                        .or_else(ignore_peer_closed)
                        .context("error responding to GetDriverInfo")?;
                }
            }
            Ok(())
        })
        .await?;
    Ok(())
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
                        .or_else(ignore_peer_closed)
                        .context("error responding to MatchDriver")?;
                }
                DriverIndexRequest::WaitForBaseDrivers { responder } => {
                    match indexer.base_repo.borrow_mut().deref_mut() {
                        BaseRepo::Resolved(_) => {
                            responder
                                .send()
                                .or_else(ignore_peer_closed)
                                .context("error responding to WaitForBaseDrivers")?;
                        }
                        BaseRepo::NotResolved(waiters) => {
                            waiters.push(responder);
                        }
                    }
                }
                DriverIndexRequest::MatchDriversV1 { args, responder } => {
                    responder
                        .send(&mut indexer.match_drivers_v1(args))
                        .or_else(ignore_peer_closed)
                        .context("error responding to MatchDriversV1")?;
                }
            }
            Ok(())
        })
        .await?;
    Ok(())
}

async fn load_base_drivers(
    indexer: Rc<Indexer>,
    resolver: fidl_fuchsia_pkg::PackageResolverProxy,
) -> Result<(), anyhow::Error> {
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
    let res = resolver.resolve(
        "fuchsia-pkg://fuchsia.com/driver-manager-base-config",
        &mut std::iter::empty(),
        dir_server_end,
    );
    res.await?.map_err(|e| anyhow::anyhow!("Failed to resolve package: {:?}", e))?;
    let data = io_util::open_file(
        &dir,
        std::path::Path::new("config/base-driver-manifest.json"),
        fio::OPEN_RIGHT_READABLE,
    )?;

    let data: String = io_util::read_file(&data).await.context("Failed to read base manifest")?;
    let drivers: Vec<JsonDriver> = serde_json::from_str(&data)?;
    let mut resolved_drivers = std::vec::Vec::new();
    for driver in drivers {
        let url = match url::Url::parse(&driver.driver_url) {
            Ok(u) => u,
            Err(e) => {
                log::error!("Found bad base driver url: {}: error: {}", driver.driver_url, e);
                continue;
            }
        };
        let resolved_driver = match ResolvedDriver::resolve(url, &resolver).await {
            Ok(r) => r,
            Err(e) => {
                log::error!("Error resolving base driver url: {}: error: {}", driver.driver_url, e);
                continue;
            }
        };
        log::info!("Found base driver: {}", resolved_driver.component_url.to_string());
        resolved_drivers.push(resolved_driver);
    }
    indexer.load_base_repo(BaseRepo::Resolved(resolved_drivers));
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    // This is to make sure driver_index's logs show up in serial.
    fuchsia_syslog::init_with_tags(&["driver"]).unwrap();

    let mut service_fs = ServiceFs::new_local();

    service_fs.dir("svc").add_fidl_service(IncomingRequest::DriverIndexProtocol);
    service_fs.dir("svc").add_fidl_service(IncomingRequest::DriverDevelopmentProtocol);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    let (resolver, resolver_stream) =
        fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_pkg::PackageResolverMarker>()
            .unwrap();

    let boot = io_util::open_directory_in_namespace("/boot", fio::OPEN_RIGHT_READABLE)
        .context("Failed to open /boot")?;
    let drivers =
        load_boot_drivers(boot).await.context("Failed to load boot drivers").map_err(log_error)?;

    let mut should_load_base_drivers = true;

    for argument in std::env::args() {
        if argument == "--no-base-drivers" {
            should_load_base_drivers = false;
            log::info!("Not loading base drivers");
        }
    }

    let index = Rc::new(Indexer::new(drivers, BaseRepo::NotResolved(std::vec![])));
    let (res1, res2, _) = futures::future::join3(
        async {
            package_resolver::serve(resolver_stream)
                .await
                .context("Error running package resolver")
                .map_err(log_error)
        },
        async {
            if should_load_base_drivers {
                load_base_drivers(index.clone(), resolver)
                    .await
                    .context("Error loading base packages")
                    .map_err(log_error)
            } else {
                Ok(())
            }
        },
        async {
            service_fs
                .for_each_concurrent(None, |request: IncomingRequest| async {
                    // match on `request` and handle each protocol.
                    match request {
                        IncomingRequest::DriverIndexProtocol(stream) => {
                            run_index_server(index.clone(), stream).await
                        }
                        IncomingRequest::DriverDevelopmentProtocol(stream) => {
                            run_driver_development_server(index.clone(), stream).await
                        }
                    }
                    .unwrap_or_else(|e| log::error!("Error running index_server: {:?}", e))
                })
                .await;
        },
    )
    .await;

    res1?;
    res2?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    async fn run_resolver_server(
        stream: fidl_fuchsia_pkg::PackageResolverRequestStream,
    ) -> Result<(), anyhow::Error> {
        stream
            .map(|result| result.context("failed request"))
            .try_for_each(|request| async {
                match request {
                    fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                        package_url: _,
                        selectors: _,
                        dir,
                        responder,
                    } => {
                        let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_DIRECTORY;
                        io_util::node::connect_in_namespace("/pkg", flags, dir.into_channel())
                            .unwrap();
                        responder.send(&mut Ok(())).context("error sending response")?;
                    }
                    fidl_fuchsia_pkg::PackageResolverRequest::GetHash {
                        package_url: _,
                        responder,
                    } => {
                        responder
                            .send(&mut Err(fuchsia_zircon::sys::ZX_ERR_UNAVAILABLE))
                            .context("error sending response")?;
                    }
                }
                Ok(())
            })
            .await?;
        Ok(())
    }

    // This test depends on '/pkg/config/drivers_for_test.json' existing in the test package.
    // The test reads that json file to determine which bind rules to read and index.
    #[fasync::run_singlethreaded(test)]
    async fn read_from_json() {
        fuchsia_syslog::init().unwrap();
        let (resolver, resolver_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_pkg::PackageResolverMarker>()
                .unwrap();

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();

        let index = Rc::new(Indexer::new(std::vec![], BaseRepo::NotResolved(std::vec![])));

        // Run two tasks: the fake resolver and the task that loads the base drivers.
        let load_base_drivers_task = load_base_drivers(index.clone(), resolver).fuse();
        let resolver_task = run_resolver_server(resolver_stream).fuse();
        futures::pin_mut!(load_base_drivers_task, resolver_task);
        futures::select! {
            result = load_base_drivers_task => {
                result.unwrap();
            },
            result = resolver_task => {
                panic!("Resolver task finished: {:?}", result);
            },
        };

        let index_task = run_index_server(index.clone(), stream).fuse();
        let test_task = async move {
            // Check the value from the 'test-bind' binary. This should match my-driver.cm
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(1)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap().unwrap();
            assert_eq!(
                "fuchsia-pkg://fuchsia.com/driver-index-unittests#meta/test-bind-component.cm"
                    .to_string(),
                result.url.unwrap(),
            );
            assert_eq!(
                "fuchsia-pkg://fuchsia.com/driver-index-unittests#driver/fake-driver.so"
                    .to_string(),
                result.driver_url.unwrap(),
            );

            // Check the value from the 'test-bind2' binary. This should match my-driver2.cm
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(2)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap().unwrap();
            assert_eq!(
                "fuchsia-pkg://fuchsia.com/driver-index-unittests#meta/test-bind2-component.cm"
                    .to_string(),
                result.url.unwrap(),
            );
            assert_eq!(
                "fuchsia-pkg://fuchsia.com/driver-index-unittests#driver/fake-driver2.so"
                    .to_string(),
                result.driver_url.unwrap(),
            );

            // Check an unknown value. This should return the NOT_FOUND error.
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(3)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap();
            assert_eq!(result, Err(Status::NOT_FOUND.into_raw()));
        }
        .fuse();

        futures::pin_mut!(index_task, test_task);
        futures::select! {
            result = index_task => {
                panic!("Index task finished: {:?}", result);
            },
            () = test_task => {},
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_bind_string() {
        use bind::compiler::Symbol;
        use bind::compiler::SymbolicInstruction;
        use bind::compiler::SymbolicInstructionInfo;
        use bind::parser::bind_library::ValueType;

        // Make the bind instructions.
        let always_match = bind::compiler::BindRules {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::AbortIfNotEqual {
                    lhs: Symbol::Key("my-key".to_string(), ValueType::Str),
                    rhs: Symbol::StringValue("test-value".to_string()),
                },
            }],
            symbol_table: std::collections::HashMap::new(),
            use_new_bytecode: true,
        };
        let always_match = DecodedBindRules::new(
            bind::bytecode_encoder::encode_v2::encode_to_bytecode_v2(always_match).unwrap(),
        )
        .unwrap();

        // Make our driver.
        let base_repo = BaseRepo::Resolved(std::vec![ResolvedDriver {
            component_url: url::Url::parse("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm")
                .unwrap(),
            driver_path: "meta/my-driver.so".to_string(),
            bind_rules: always_match.clone(),
        },]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();

        let index = Rc::new(Indexer::new(std::vec![], base_repo));

        let index_task = run_index_server(index.clone(), stream).fuse();
        let test_task = async move {
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::StringValue("my-key".to_string())),
                value: Some(fdf::NodePropertyValue::StringValue("test-value".to_string())),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };

            let result = proxy.match_drivers_v1(args).await.unwrap().unwrap();
            assert_eq!(1, result.len());
            assert_eq!(
                "fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm",
                result[0].url.as_ref().unwrap().as_str(),
            );
        }
        .fuse();

        futures::pin_mut!(index_task, test_task);
        futures::select! {
            result = index_task => {
                panic!("Index task finished: {:?}", result);
            },
            () = test_task => {},
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_match_drivers_v1() {
        // Make the bind instructions.
        let always_match = bind::compiler::BindRules {
            instructions: vec![],
            symbol_table: std::collections::HashMap::new(),
            use_new_bytecode: true,
        };
        let always_match = DecodedBindRules::new(
            bind::bytecode_encoder::encode_v2::encode_to_bytecode_v2(always_match).unwrap(),
        )
        .unwrap();

        // Make two drivers that will bind to anything.
        let base_repo = BaseRepo::Resolved(std::vec![
            ResolvedDriver {
                component_url: url::Url::parse(
                    "fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm"
                )
                .unwrap(),
                driver_path: "meta/my-driver.so".to_string(),
                bind_rules: always_match.clone(),
            },
            ResolvedDriver {
                component_url: url::Url::parse(
                    "fuchsia-pkg://fuchsia.com/package#driver/my-driver2.cm"
                )
                .unwrap(),
                driver_path: "meta/my-driver2.so".to_string(),
                bind_rules: always_match.clone(),
            }
        ]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();

        let index = Rc::new(Indexer::new(std::vec![], base_repo));

        let index_task = run_index_server(index.clone(), stream).fuse();
        let test_task = async move {
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(2)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };

            let result = proxy.match_drivers_v1(args).await.unwrap().unwrap();
            assert_eq!(2, result.len());
            assert_eq!(
                "fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm",
                result[0].url.as_ref().unwrap().as_str(),
            );
            assert_eq!(
                "fuchsia-pkg://fuchsia.com/package#driver/my-driver2.cm",
                result[1].url.as_ref().unwrap().as_str(),
            );
        }
        .fuse();

        futures::pin_mut!(index_task, test_task);
        futures::select! {
            result = index_task => {
                panic!("Index task finished: {:?}", result);
            },
            () = test_task => {},
        }
    }

    // This test relies on two drivers existing in the /pkg/ directory of the
    // test package.
    #[fasync::run_singlethreaded(test)]
    async fn test_boot_drivers() {
        fuchsia_syslog::init().unwrap();
        let boot = io_util::open_directory_in_namespace("/pkg", fio::OPEN_RIGHT_READABLE).unwrap();
        let drivers = load_boot_drivers(boot).await.unwrap();

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();

        let index = Rc::new(Indexer::new(drivers, BaseRepo::NotResolved(vec![])));

        let index_task = run_index_server(index.clone(), stream).fuse();
        let test_task = async move {
            // Check the value from the 'test-bind' binary. This should match my-driver.cm
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(1)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap().unwrap();
            assert_eq!(
                "fuchsia-boot:///#meta/test-bind-component.cm".to_string(),
                result.url.unwrap(),
            );
            assert_eq!(
                "fuchsia-boot:///#driver/fake-driver.so".to_string(),
                result.driver_url.unwrap(),
            );

            // Check the value from the 'test-bind2' binary. This should match my-driver2.cm
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(2)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap().unwrap();
            assert_eq!(
                "fuchsia-boot:///#meta/test-bind2-component.cm".to_string(),
                result.url.unwrap(),
            );
            assert_eq!(
                "fuchsia-boot:///#driver/fake-driver2.so".to_string(),
                result.driver_url.unwrap(),
            );

            // Check an unknown value. This should return the NOT_FOUND error.
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(3)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap();
            assert_eq!(result, Err(Status::NOT_FOUND.into_raw()));
        }
        .fuse();

        futures::pin_mut!(index_task, test_task);
        futures::select! {
            result = index_task => {
                panic!("Index task finished: {:?}", result);
            },
            () = test_task => {},
        }
    }
}
