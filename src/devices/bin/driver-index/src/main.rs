// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::device_group::DeviceGroup,
    crate::match_common::node_to_device_property,
    crate::resolved_driver::{load_driver, ResolvedDriver},
    anyhow::{self, Context},
    fidl_fuchsia_boot as fboot, fidl_fuchsia_driver_development as fdd,
    fidl_fuchsia_driver_framework as fdf,
    fidl_fuchsia_driver_framework::{DriverIndexRequest, DriverIndexRequestStream},
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::Status,
    futures::prelude::*,
    serde::Deserialize,
    std::{
        cell::RefCell,
        collections::HashMap,
        collections::HashSet,
        ops::Deref,
        ops::DerefMut,
        rc::Rc,
        sync::{Arc, Mutex},
    },
};

mod device_group;
mod match_common;
mod package_resolver;
mod resolved_driver;

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

async fn load_boot_drivers(
    dir: fio::DirectoryProxy,
    eager_drivers: &HashSet<url::Url>,
) -> Result<Vec<ResolvedDriver>, anyhow::Error> {
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
        if let Ok(Some(mut driver)) = driver {
            log::info!("Found boot driver: {}", driver.component_url.to_string());
            if eager_drivers.contains(&driver.component_url) {
                driver.fallback = false;
            }
            drivers.push(driver);
        }
    }
    Ok(drivers)
}

enum BaseRepo {
    // We know that Base won't update so we can store these as resolved.
    Resolved(Vec<ResolvedDriver>),
    // If it's not resolved we store the clients waiting for it.
    NotResolved(Vec<fdf::DriverIndexWaitForBaseDriversResponder>),
}

struct Indexer {
    boot_repo: Vec<ResolvedDriver>,

    // |base_repo| needs to be in a RefCell because it starts out NotResolved,
    // but will eventually resolve when base packages are available.
    base_repo: RefCell<BaseRepo>,

    // Contains the device groups. This is wrapped in a RefCell since the
    // device groups are added after the driver index server has started.
    device_groups: RefCell<HashMap<String, DeviceGroup>>,
}

impl Indexer {
    fn new(boot_repo: Vec<ResolvedDriver>, base_repo: BaseRepo) -> Indexer {
        Indexer {
            boot_repo: boot_repo,
            base_repo: RefCell::new(base_repo),
            device_groups: RefCell::new(HashMap::new()),
        }
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
            if let Ok(Some(m)) = driver.matches(&properties) {
                return Ok(m);
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
            if let Ok(Some(m)) = driver.matches(&properties) {
                return Ok(m);
            }
        }

        let groups = self.device_groups.borrow();
        for (_, group) in groups.iter() {
            if let Some(m) = group.matches(&properties) {
                return Ok(m);
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

        let mut matched_drivers: Vec<fdf::MatchedDriver> = self
            .boot_repo
            .iter()
            .chain(base_repo_iter)
            .filter_map(|driver| driver.matches(&properties).ok())
            .filter_map(|d| d)
            .collect();

        matched_drivers.append(
            &mut self
                .device_groups
                .borrow()
                .iter()
                .filter_map(|(_, driver)| driver.matches(&properties))
                .collect(),
        );

        Ok(matched_drivers)
    }

    fn add_device_group(
        &self,
        topological_path: String,
        nodes: Vec<fdf::DeviceGroupNode>,
    ) -> fdf::DriverIndexAddDeviceGroupResult {
        let mut device_groups = self.device_groups.borrow_mut();

        if device_groups.contains_key(&topological_path) {
            return Err(Status::ALREADY_EXISTS.into_raw());
        }

        device_groups
            .insert(topological_path.clone(), DeviceGroup::create(topological_path, nodes)?);
        Ok(())
    }

    fn get_driver_info(&self, driver_filter: Vec<String>) -> Vec<fdd::DriverInfo> {
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

        driver_info
    }
}

async fn run_driver_info_iterator_server(
    driver_info: Arc<Mutex<Vec<fdd::DriverInfo>>>,
    stream: fdd::DriverInfoIteratorRequestStream,
) -> Result<(), anyhow::Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            let driver_info_clone = driver_info.clone();
            match request {
                fdd::DriverInfoIteratorRequest::GetNext { responder } => {
                    let result = {
                        let mut driver_info = driver_info_clone.lock().unwrap();
                        let len = driver_info.len();
                        driver_info.split_off(len - std::cmp::min(100, len))
                    };

                    responder
                        .send(&mut result.into_iter())
                        .or_else(ignore_peer_closed)
                        .context("error responding to GetDriverInfo")?;
                }
            }
            Ok(())
        })
        .await?;
    Ok(())
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
                fdd::DriverIndexRequest::GetDriverInfo { driver_filter, iterator, .. } => {
                    let driver_info = Arc::new(Mutex::new(indexer.get_driver_info(driver_filter)));
                    let iterator = iterator.into_stream()?;
                    fasync::Task::spawn(async move {
                        run_driver_info_iterator_server(driver_info, iterator)
                            .await
                            .expect("Failed to run driver info iterator");
                    })
                    .detach();
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
                DriverIndexRequest::AddDeviceGroup { topological_path, nodes, responder } => {
                    responder
                        .send(&mut indexer.add_device_group(topological_path, nodes))
                        .or_else(ignore_peer_closed)
                        .context("error responding to AddDeviceGroup")?;
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
    eager_drivers: &HashSet<url::Url>,
) -> Result<(), anyhow::Error> {
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
    let res =
        resolver.resolve("fuchsia-pkg://fuchsia.com/driver-manager-base-config", dir_server_end);
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
        let mut resolved_driver = match ResolvedDriver::resolve(url, &resolver).await {
            Ok(r) => r,
            Err(e) => {
                log::error!("Error resolving base driver url: {}: error: {}", driver.driver_url, e);
                continue;
            }
        };
        if eager_drivers.contains(&resolved_driver.component_url) {
            resolved_driver.fallback = false;
        }
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

    let boot_args = fuchsia_component::client::connect_to_protocol::<fboot::ArgumentsMarker>()
        .context("Failed to connect to boot arguments service")?;
    let eager_drivers: HashSet<url::Url> = boot_args
        .get_string("devmgr.bind-eager")
        .await
        .context("Failed to get eager drivers from boot arguments")?
        .unwrap_or_default()
        .split(",")
        .filter(|url| !url.is_empty())
        .filter_map(|url| url::Url::parse(url).ok())
        .collect();

    let boot = io_util::open_directory_in_namespace("/boot", fio::OPEN_RIGHT_READABLE)
        .context("Failed to open /boot")?;
    let drivers = load_boot_drivers(boot, &eager_drivers)
        .await
        .context("Failed to load boot drivers")
        .map_err(log_error)?;

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
                load_base_drivers(index.clone(), resolver, &eager_drivers)
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
    use {
        bind::{
            compiler::{
                CompiledBindRules, CompositeBindRules, CompositeNode, Symbol, SymbolicInstruction,
                SymbolicInstructionInfo,
            },
            interpreter::decode_bind_rules::DecodedRules,
            parser::bind_library::ValueType,
        },
        std::collections::HashMap,
    };

    fn create_matched_driver_info(
        url: String,
        driver_url: String,
        colocate: bool,
    ) -> fdf::MatchedDriverInfo {
        fdf::MatchedDriverInfo {
            url: Some(url),
            driver_url: Some(driver_url),
            colocate: Some(colocate),
            ..fdf::MatchedDriverInfo::EMPTY
        }
    }

    async fn run_resolver_server(
        stream: fidl_fuchsia_pkg::PackageResolverRequestStream,
    ) -> Result<(), anyhow::Error> {
        stream
            .map(|result| result.context("failed request"))
            .try_for_each(|request| async {
                match request {
                    fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                        package_url: _,
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

        let eager_drivers = HashSet::new();

        // Run two tasks: the fake resolver and the task that loads the base drivers.
        let load_base_drivers_task =
            load_base_drivers(index.clone(), resolver, &eager_drivers).fuse();
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

            let expected_url =
                "fuchsia-pkg://fuchsia.com/driver-index-unittests#meta/test-bind-component.cm"
                    .to_string();
            let expected_driver_url =
                "fuchsia-pkg://fuchsia.com/driver-index-unittests#driver/fake-driver.so"
                    .to_string();
            let expected_result = fdf::MatchedDriver::Driver(create_matched_driver_info(
                expected_url,
                expected_driver_url,
                true,
            ));
            assert_eq!(expected_result, result);

            // Check the value from the 'test-bind2' binary. This should match my-driver2.cm
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(2)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap().unwrap();

            let expected_url =
                "fuchsia-pkg://fuchsia.com/driver-index-unittests#meta/test-bind2-component.cm"
                    .to_string();
            let expected_driver_url =
                "fuchsia-pkg://fuchsia.com/driver-index-unittests#driver/fake-driver2.so"
                    .to_string();
            let expected_result = fdf::MatchedDriver::Driver(create_matched_driver_info(
                expected_url,
                expected_driver_url,
                false,
            ));
            assert_eq!(expected_result, result);

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
        // Make the bind instructions.
        let always_match = bind::compiler::BindRules {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::AbortIfNotEqual {
                    lhs: Symbol::Key("my-key".to_string(), ValueType::Str),
                    rhs: Symbol::StringValue("test-value".to_string()),
                },
            }],
            symbol_table: HashMap::new(),
            use_new_bytecode: true,
        };
        let always_match = DecodedRules::new(
            bind::bytecode_encoder::encode_v2::encode_to_bytecode_v2(always_match).unwrap(),
        )
        .unwrap();

        // Make our driver.
        let base_repo = BaseRepo::Resolved(std::vec![ResolvedDriver {
            component_url: url::Url::parse("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm")
                .unwrap(),
            v1_driver_path: None,
            bind_rules: always_match.clone(),
            colocate: false,
            fallback: false,
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

            let expected_result = vec![fdf::MatchedDriver::Driver(fdf::MatchedDriverInfo {
                url: Some("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm".to_string()),
                colocate: Some(false),
                ..fdf::MatchedDriverInfo::EMPTY
            })];

            assert_eq!(expected_result, result);
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
        let always_match = DecodedRules::new(
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
                v1_driver_path: Some("meta/my-driver.so".to_string()),
                bind_rules: always_match.clone(),
                colocate: false,
                fallback: false,
            },
            ResolvedDriver {
                component_url: url::Url::parse(
                    "fuchsia-pkg://fuchsia.com/package#driver/my-driver2.cm"
                )
                .unwrap(),
                v1_driver_path: Some("meta/my-driver2.so".to_string()),
                bind_rules: always_match.clone(),
                colocate: false,
                fallback: false,
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

            let expected_url_1 =
                "fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm".to_string();
            let expected_driver_url_1 =
                "fuchsia-pkg://fuchsia.com/package#meta/my-driver.so".to_string();
            let expected_url_2 =
                "fuchsia-pkg://fuchsia.com/package#driver/my-driver2.cm".to_string();
            let expected_driver_url_2 =
                "fuchsia-pkg://fuchsia.com/package#meta/my-driver2.so".to_string();

            let expected_result = vec![
                fdf::MatchedDriver::Driver(create_matched_driver_info(
                    expected_url_1,
                    expected_driver_url_1,
                    false,
                )),
                fdf::MatchedDriver::Driver(create_matched_driver_info(
                    expected_url_2,
                    expected_driver_url_2,
                    false,
                )),
            ];

            assert_eq!(expected_result, result);
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
    async fn test_load_fallback_driver() {
        const DRIVER_URL: &str = "fuchsia-boot:///#meta/test-fallback-component.cm";
        let driver_url = url::Url::parse(&DRIVER_URL).unwrap();
        let pkg = io_util::open_directory_in_namespace("/pkg", fio::OPEN_RIGHT_READABLE)
            .context("Failed to open /pkg")
            .unwrap();
        let fallback_driver =
            load_driver(&pkg, driver_url).await.unwrap().expect("Fallback driver was not loaded");
        assert!(fallback_driver.fallback);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_eager_fallback_boot_driver() {
        let eager_driver_component_url =
            url::Url::parse("fuchsia-boot:///#meta/test-fallback-component.cm").unwrap();

        let boot = io_util::open_directory_in_namespace("/pkg", fio::OPEN_RIGHT_READABLE).unwrap();
        let drivers = load_boot_drivers(boot, &HashSet::from([eager_driver_component_url.clone()]))
            .await
            .unwrap();
        assert!(
            !drivers
                .iter()
                .find(|driver| driver.component_url == eager_driver_component_url)
                .expect("Fallback driver did not load")
                .fallback
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_eager_fallback_base_driver() {
        let eager_driver_component_url = url::Url::parse(
            "fuchsia-pkg://fuchsia.com/driver-index-unittests#meta/test-fallback-component.cm",
        )
        .unwrap();

        fuchsia_syslog::init().unwrap();
        let (resolver, resolver_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_pkg::PackageResolverMarker>()
                .unwrap();

        let index = Rc::new(Indexer::new(std::vec![], BaseRepo::NotResolved(std::vec![])));

        let eager_drivers = HashSet::from([eager_driver_component_url.clone()]);

        let load_base_drivers_task =
            load_base_drivers(Rc::clone(&index), resolver, &eager_drivers).fuse();
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

        let base_repo = index.base_repo.borrow();
        match *base_repo {
            BaseRepo::Resolved(ref drivers) => {
                assert!(
                    !drivers
                        .iter()
                        .find(|driver| driver.component_url == eager_driver_component_url)
                        .expect("Fallback driver did not load")
                        .fallback
                );
            }
            _ => {
                panic!("Base repo was not resolved");
            }
        }
    }

    // This test relies on two drivers existing in the /pkg/ directory of the
    // test package.
    #[fasync::run_singlethreaded(test)]
    async fn test_boot_drivers() {
        fuchsia_syslog::init().unwrap();
        let boot = io_util::open_directory_in_namespace("/pkg", fio::OPEN_RIGHT_READABLE).unwrap();
        let drivers = load_boot_drivers(boot, &HashSet::new()).await.unwrap();

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

            let expected_url = "fuchsia-boot:///#meta/test-bind-component.cm".to_string();
            let expected_driver_url = "fuchsia-boot:///#driver/fake-driver.so".to_string();
            let expected_result = fdf::MatchedDriver::Driver(create_matched_driver_info(
                expected_url,
                expected_driver_url,
                true,
            ));
            assert_eq!(expected_result, result);

            // Check the value from the 'test-bind2' binary. This should match my-driver2.cm
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(bind::ddk_bind_constants::BIND_PROTOCOL)),
                value: Some(fdf::NodePropertyValue::IntValue(2)),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };
            let result = proxy.match_driver(args).await.unwrap().unwrap();

            let expected_url = "fuchsia-boot:///#meta/test-bind2-component.cm".to_string();
            let expected_driver_url = "fuchsia-boot:///#driver/fake-driver2.so".to_string();
            let expected_result = fdf::MatchedDriver::Driver(create_matched_driver_info(
                expected_url,
                expected_driver_url,
                false,
            ));
            assert_eq!(expected_result, result);

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
    async fn test_bind_composite() {
        // Create the Composite Bind rules.
        let primary_node_inst = vec![SymbolicInstructionInfo {
            location: None,
            instruction: SymbolicInstruction::AbortIfNotEqual {
                lhs: Symbol::Key("trembler".to_string(), ValueType::Str),
                rhs: Symbol::StringValue("thrasher".to_string()),
            },
        }];

        let additional_node_inst = vec![
            SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::AbortIfNotEqual {
                    lhs: Symbol::Key("thrasher".to_string(), ValueType::Str),
                    rhs: Symbol::StringValue("catbird".to_string()),
                },
            },
            SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::AbortIfNotEqual {
                    lhs: Symbol::Key("catbird".to_string(), ValueType::Number),
                    rhs: Symbol::NumberValue(1),
                },
            },
        ];

        let bind_rules = CompositeBindRules {
            device_name: "mimid".to_string(),
            symbol_table: HashMap::new(),
            primary_node: CompositeNode {
                name: "catbird".to_string(),
                instructions: primary_node_inst,
            },
            additional_nodes: vec![CompositeNode {
                name: "mockingbird".to_string(),
                instructions: additional_node_inst,
            }],
        };

        let bytecode = CompiledBindRules::CompositeBind(bind_rules).encode_to_bytecode().unwrap();
        let rules = DecodedRules::new(bytecode).unwrap();

        // Make the composite driver.
        let base_repo = BaseRepo::Resolved(std::vec![ResolvedDriver {
            component_url: url::Url::parse("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm")
                .unwrap(),
            v1_driver_path: None,
            bind_rules: rules,
            colocate: false,
            fallback: false,
        },]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();

        let index = Rc::new(Indexer::new(std::vec![], base_repo));

        let index_task = run_index_server(index.clone(), stream).fuse();
        let test_task = async move {
            // Match primary node.
            let property = fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::StringValue("trembler".to_string())),
                value: Some(fdf::NodePropertyValue::StringValue("thrasher".to_string())),
                ..fdf::NodeProperty::EMPTY
            };
            let args =
                fdf::NodeAddArgs { properties: Some(vec![property]), ..fdf::NodeAddArgs::EMPTY };

            let result = proxy.match_driver(args).await.unwrap().unwrap();

            let expected_driver_info = fdf::MatchedDriverInfo {
                url: Some("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm".to_string()),
                colocate: Some(false),
                ..fdf::MatchedDriverInfo::EMPTY
            };
            let expected_result = fdf::MatchedDriver::CompositeDriver(fdf::MatchedCompositeInfo {
                node_index: Some(0),
                num_nodes: Some(2),
                composite_name: Some("mimid".to_string()),
                node_names: Some(vec!["catbird".to_string(), "mockingbird".to_string()]),
                driver_info: Some(expected_driver_info),
                ..fdf::MatchedCompositeInfo::EMPTY
            });
            assert_eq!(expected_result, result);

            // Match secondary node.
            let args = fdf::NodeAddArgs {
                properties: Some(vec![
                    fdf::NodeProperty {
                        key: Some(fdf::NodePropertyKey::StringValue("thrasher".to_string())),
                        value: Some(fdf::NodePropertyValue::StringValue("catbird".to_string())),
                        ..fdf::NodeProperty::EMPTY
                    },
                    fdf::NodeProperty {
                        key: Some(fdf::NodePropertyKey::StringValue("catbird".to_string())),
                        value: Some(fdf::NodePropertyValue::IntValue(1)),
                        ..fdf::NodeProperty::EMPTY
                    },
                ]),
                ..fdf::NodeAddArgs::EMPTY
            };

            let result = proxy.match_driver(args).await.unwrap().unwrap();

            let expected_driver_info = fdf::MatchedDriverInfo {
                url: Some("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm".to_string()),
                colocate: Some(false),
                ..fdf::MatchedDriverInfo::EMPTY
            };
            let expected_result = fdf::MatchedDriver::CompositeDriver(fdf::MatchedCompositeInfo {
                node_index: Some(1),
                num_nodes: Some(2),
                composite_name: Some("mimid".to_string()),
                node_names: Some(vec!["catbird".to_string(), "mockingbird".to_string()]),
                driver_info: Some(expected_driver_info),
                ..fdf::MatchedCompositeInfo::EMPTY
            });
            assert_eq!(expected_result, result);
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
    async fn test_device_group_match() {
        let base_repo = BaseRepo::Resolved(std::vec![]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();
        let index = Rc::new(Indexer::new(std::vec![], base_repo));
        let index_task = run_index_server(index.clone(), stream).fuse();

        let test_task = async move {
            let node_properties = vec![
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(1)),
                    value: Some(fdf::NodePropertyValue::IntValue(200)),
                    ..fdf::NodeProperty::EMPTY
                },
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::StringValue("lapwing".to_string())),
                    value: Some(fdf::NodePropertyValue::StringValue("plover".to_string())),
                    ..fdf::NodeProperty::EMPTY
                },
            ];

            let nodes = &mut [fdf::DeviceGroupNode {
                name: "whimbrel".to_string(),
                properties: node_properties,
            }];

            proxy.add_device_group("test/path", &mut nodes.iter_mut()).await.unwrap().unwrap();

            let device_properties_match = vec![
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(1)),
                    value: Some(fdf::NodePropertyValue::IntValue(200)),
                    ..fdf::NodeProperty::EMPTY
                },
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::StringValue("lapwing".to_string())),
                    value: Some(fdf::NodePropertyValue::StringValue("plover".to_string())),
                    ..fdf::NodeProperty::EMPTY
                },
            ];
            let match_args = fdf::NodeAddArgs {
                properties: Some(device_properties_match),
                ..fdf::NodeAddArgs::EMPTY
            };

            let result = proxy.match_driver(match_args).await.unwrap().unwrap();
            assert_eq!(
                fdf::MatchedDriver::DeviceGroup(fdf::MatchedDeviceGroupInfo {
                    topological_path: Some("test/path".to_string()),
                    node_index: Some(0),
                    num_nodes: Some(1),
                    node_names: Some(vec!["whimbrel".to_string()]),
                    ..fdf::MatchedDeviceGroupInfo::EMPTY
                }),
                result
            );

            let device_properties_mismatch = vec![
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(1)),
                    value: Some(fdf::NodePropertyValue::IntValue(200)),
                    ..fdf::NodeProperty::EMPTY
                },
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::StringValue("lapwing".to_string())),
                    value: Some(fdf::NodePropertyValue::StringValue("dotterel".to_string())),
                    ..fdf::NodeProperty::EMPTY
                },
            ];
            let mismatch_args = fdf::NodeAddArgs {
                properties: Some(device_properties_mismatch),
                ..fdf::NodeAddArgs::EMPTY
            };

            let result = proxy.match_driver(mismatch_args).await.unwrap();
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
    async fn test_device_group_match_v1() {
        let always_match_rules = bind::compiler::BindRules {
            instructions: vec![],
            symbol_table: std::collections::HashMap::new(),
            use_new_bytecode: true,
        };
        let always_match = DecodedRules::new(
            bind::bytecode_encoder::encode_v2::encode_to_bytecode_v2(always_match_rules).unwrap(),
        )
        .unwrap();

        let base_repo = BaseRepo::Resolved(std::vec![ResolvedDriver {
            component_url: url::Url::parse("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm")
                .unwrap(),
            v1_driver_path: None,
            bind_rules: always_match,
            colocate: false,
            fallback: false,
        },]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();
        let index = Rc::new(Indexer::new(std::vec![], base_repo));
        let index_task = run_index_server(index.clone(), stream).fuse();

        let test_task = async move {
            let node_properties = vec![
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(1)),
                    value: Some(fdf::NodePropertyValue::IntValue(200)),
                    ..fdf::NodeProperty::EMPTY
                },
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::StringValue("lapwing".to_string())),
                    value: Some(fdf::NodePropertyValue::StringValue("plover".to_string())),
                    ..fdf::NodeProperty::EMPTY
                },
            ];

            let nodes = &mut [fdf::DeviceGroupNode {
                name: "whimbrel".to_string(),
                properties: node_properties,
            }];

            proxy.add_device_group("test/path", &mut nodes.iter_mut()).await.unwrap().unwrap();

            let device_properties_match = vec![
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(1)),
                    value: Some(fdf::NodePropertyValue::IntValue(200)),
                    ..fdf::NodeProperty::EMPTY
                },
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::StringValue("lapwing".to_string())),
                    value: Some(fdf::NodePropertyValue::StringValue("plover".to_string())),
                    ..fdf::NodeProperty::EMPTY
                },
            ];
            let match_args = fdf::NodeAddArgs {
                properties: Some(device_properties_match),
                ..fdf::NodeAddArgs::EMPTY
            };

            let result = proxy.match_drivers_v1(match_args).await.unwrap().unwrap();
            assert_eq!(2, result.len());
            assert_eq!(
                vec![
                    fdf::MatchedDriver::Driver(fdf::MatchedDriverInfo {
                        url: Some(
                            "fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm".to_string()
                        ),
                        colocate: Some(false),
                        ..fdf::MatchedDriverInfo::EMPTY
                    }),
                    fdf::MatchedDriver::DeviceGroup(fdf::MatchedDeviceGroupInfo {
                        topological_path: Some("test/path".to_string()),
                        node_index: Some(0),
                        num_nodes: Some(1),
                        node_names: Some(vec!["whimbrel".to_string()]),
                        ..fdf::MatchedDeviceGroupInfo::EMPTY
                    })
                ],
                result
            );

            let device_properties_mismatch = vec![
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(1)),
                    value: Some(fdf::NodePropertyValue::IntValue(200)),
                    ..fdf::NodeProperty::EMPTY
                },
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::StringValue("lapwing".to_string())),
                    value: Some(fdf::NodePropertyValue::StringValue("dotterel".to_string())),
                    ..fdf::NodeProperty::EMPTY
                },
            ];
            let mismatch_args = fdf::NodeAddArgs {
                properties: Some(device_properties_mismatch),
                ..fdf::NodeAddArgs::EMPTY
            };

            let result = proxy.match_drivers_v1(mismatch_args).await.unwrap().unwrap();
            assert_eq!(1, result.len());
            assert_eq!(
                vec![fdf::MatchedDriver::Driver(fdf::MatchedDriverInfo {
                    url: Some("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm".to_string()),
                    colocate: Some(false),
                    ..fdf::MatchedDriverInfo::EMPTY
                }),],
                result
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
    async fn test_add_device_group_duplicate_path() {
        let always_match_rules = bind::compiler::BindRules {
            instructions: vec![],
            symbol_table: std::collections::HashMap::new(),
            use_new_bytecode: true,
        };
        let always_match = DecodedRules::new(
            bind::bytecode_encoder::encode_v2::encode_to_bytecode_v2(always_match_rules).unwrap(),
        )
        .unwrap();

        let base_repo = BaseRepo::Resolved(std::vec![ResolvedDriver {
            component_url: url::Url::parse("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm")
                .unwrap(),
            v1_driver_path: None,
            bind_rules: always_match,
            colocate: false,
            fallback: false,
        },]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();
        let index = Rc::new(Indexer::new(std::vec![], base_repo));
        let index_task = run_index_server(index.clone(), stream).fuse();

        let test_task = async move {
            let node_properties = vec![
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(1)),
                    value: Some(fdf::NodePropertyValue::IntValue(200)),
                    ..fdf::NodeProperty::EMPTY
                },
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::StringValue("lapwing".to_string())),
                    value: Some(fdf::NodePropertyValue::StringValue("plover".to_string())),
                    ..fdf::NodeProperty::EMPTY
                },
            ];

            let nodes = &mut [fdf::DeviceGroupNode {
                name: "whimbrel".to_string(),
                properties: node_properties,
            }];

            proxy.add_device_group("test/path", &mut nodes.iter_mut()).await.unwrap().unwrap();

            let duplicate_node_properties = vec![fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(200)),
                value: Some(fdf::NodePropertyValue::IntValue(2)),
                ..fdf::NodeProperty::EMPTY
            }];

            let duplicate_nodes = &mut [fdf::DeviceGroupNode {
                name: "dunlin".to_string(),
                properties: duplicate_node_properties,
            }];

            let result =
                proxy.add_device_group("test/path", &mut duplicate_nodes.iter_mut()).await.unwrap();
            assert_eq!(Err(Status::ALREADY_EXISTS.into_raw()), result);
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
    async fn test_add_device_group_duplicate_key() {
        let always_match_rules = bind::compiler::BindRules {
            instructions: vec![],
            symbol_table: std::collections::HashMap::new(),
            use_new_bytecode: true,
        };
        let always_match = DecodedRules::new(
            bind::bytecode_encoder::encode_v2::encode_to_bytecode_v2(always_match_rules).unwrap(),
        )
        .unwrap();

        let base_repo = BaseRepo::Resolved(std::vec![ResolvedDriver {
            component_url: url::Url::parse("fuchsia-pkg://fuchsia.com/package#driver/my-driver.cm")
                .unwrap(),
            v1_driver_path: None,
            bind_rules: always_match,
            colocate: false,
            fallback: false,
        },]);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fdf::DriverIndexMarker>().unwrap();
        let index = Rc::new(Indexer::new(std::vec![], base_repo));
        let index_task = run_index_server(index.clone(), stream).fuse();

        let test_task = async move {
            let node_properties = vec![
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(20)),
                    value: Some(fdf::NodePropertyValue::IntValue(200)),
                    ..fdf::NodeProperty::EMPTY
                },
                fdf::NodeProperty {
                    key: Some(fdf::NodePropertyKey::IntValue(20)),
                    value: Some(fdf::NodePropertyValue::StringValue("plover".to_string())),
                    ..fdf::NodeProperty::EMPTY
                },
            ];

            let nodes = &mut [fdf::DeviceGroupNode {
                name: "whimbrel".to_string(),
                properties: node_properties,
            }];

            let result = proxy.add_device_group("test/path", &mut nodes.iter_mut()).await.unwrap();
            assert_eq!(Err(Status::INVALID_ARGS.into_raw()), result);
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
