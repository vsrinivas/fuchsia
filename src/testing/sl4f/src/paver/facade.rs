// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::get_proxy_or_connect;
use anyhow::{bail, Error};
use fidl_fuchsia_paver::{PaverMarker, PaverProxy};
use fuchsia_zircon::Status;
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};

use super::types::{Asset, Configuration, ConfigurationStatus};

/// Facade providing access to paver service.
#[derive(Debug)]
pub struct PaverFacade {
    proxy: RwLock<Option<PaverProxy>>,
}

impl PaverFacade {
    /// Creates a new [PaverFacade] with no active connection to the paver service.
    pub fn new() -> Self {
        Self { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: PaverProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    /// Return a cached connection to the paver service, or try to connect and cache the connection
    /// for later.
    fn proxy(&self) -> Result<PaverProxy, Error> {
        get_proxy_or_connect::<PaverMarker>(&self.proxy)
    }

    /// Queries the active boot configuration, if the current bootloader supports it.
    ///
    /// # Errors
    ///
    /// Returns an Err(_) if
    ///  * connecting to the paver service fails, or
    ///  * the paver service returns an unexpected error
    pub(super) async fn query_active_configuration(
        &self,
    ) -> Result<QueryActiveConfigurationResult, Error> {
        let (boot_manager, boot_manager_server_end) = fidl::endpoints::create_proxy()?;

        self.proxy()?.find_boot_manager(boot_manager_server_end)?;

        match boot_manager.query_active_configuration().await {
            Ok(Ok(config)) => Ok(QueryActiveConfigurationResult::Success(config.into())),
            Ok(Err(err)) => bail!("unexpected failure status: {}", err),
            Err(fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. }) => {
                Ok(QueryActiveConfigurationResult::NotSupported)
            }
            Err(err) => bail!("unexpected failure status: {}", err),
        }
    }

    /// Queries the current boot configuration, if the current bootloader supports it.
    ///
    /// # Errors
    ///
    /// Returns an Err(_) if
    ///  * connecting to the paver service fails, or
    ///  * the paver service returns an unexpected error
    pub(super) async fn query_current_configuration(
        &self,
    ) -> Result<QueryCurrentConfigurationResult, Error> {
        let (boot_manager, boot_manager_server_end) = fidl::endpoints::create_proxy()?;

        self.proxy()?.find_boot_manager(boot_manager_server_end)?;

        match boot_manager.query_current_configuration().await {
            Ok(Ok(config)) => Ok(QueryCurrentConfigurationResult::Success(config.into())),
            Ok(Err(err)) => bail!("unexpected failure status: {}", err),
            Err(fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. }) => {
                Ok(QueryCurrentConfigurationResult::NotSupported)
            }
            Err(err) => bail!("unexpected failure status: {}", err),
        }
    }

    /// Queries the bootable status of the given configuration, if the current bootloader supports
    /// it.
    ///
    /// # Errors
    ///
    /// Returns an Err(_) if
    ///  * connecting to the paver service fails, or
    ///  * the paver service returns an unexpected error
    pub(super) async fn query_configuration_status(
        &self,
        args: QueryConfigurationStatusRequest,
    ) -> Result<QueryConfigurationStatusResult, Error> {
        let (boot_manager, boot_manager_server_end) = fidl::endpoints::create_proxy()?;

        self.proxy()?.find_boot_manager(boot_manager_server_end)?;

        match boot_manager.query_configuration_status(args.configuration.into()).await {
            Ok(Ok(status)) => Ok(QueryConfigurationStatusResult::Success(status.into())),
            Ok(Err(err)) => bail!("unexpected failure status: {}", err),
            Err(fidl::Error::ClientChannelClosed { status: Status::NOT_SUPPORTED, .. }) => {
                Ok(QueryConfigurationStatusResult::NotSupported)
            }
            Err(err) => bail!("unexpected failure status: {}", err),
        }
    }

    /// Given a configuration and asset identifier, read that image and return it as a base64
    /// encoded String.
    ///
    /// # Errors
    ///
    /// Returns an Err(_) if
    ///  * connecting to the paver service fails, or
    ///  * the paver service returns an unexpected error
    pub(super) async fn read_asset(&self, args: ReadAssetRequest) -> Result<String, Error> {
        let (data_sink, data_sink_server_end) = fidl::endpoints::create_proxy()?;

        self.proxy()?.find_data_sink(data_sink_server_end)?;

        let buffer = data_sink
            .read_asset(args.configuration.into(), args.asset.into())
            .await?
            .map_err(Status::from_raw)?;

        let mut res = vec![0; buffer.size as usize];
        buffer.vmo.read(&mut res[..], 0)?;
        Ok(base64::encode(&res))
    }
}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub(super) enum QueryActiveConfigurationResult {
    Success(Configuration),
    NotSupported,
}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub(super) enum QueryCurrentConfigurationResult {
    Success(Configuration),
    NotSupported,
}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub(super) struct QueryConfigurationStatusRequest {
    configuration: Configuration,
}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub(super) enum QueryConfigurationStatusResult {
    Success(ConfigurationStatus),
    NotSupported,
}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq, Clone)]
pub(super) struct ReadAssetRequest {
    configuration: Configuration,
    asset: Asset,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common_utils::test::assert_value_round_trips_as;
    use fidl_fuchsia_paver::{
        BootManagerRequest, BootManagerRequestStream, DataSinkRequest, DataSinkRequestStream,
        PaverRequest,
    };
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;
    use serde_json::json;

    #[test]
    fn serde_query_active_configuration_result() {
        assert_value_round_trips_as(
            QueryActiveConfigurationResult::NotSupported,
            json!("not_supported"),
        );
        assert_value_round_trips_as(
            QueryActiveConfigurationResult::Success(Configuration::A),
            json!({"success": "a"}),
        );
    }

    #[test]
    fn serde_query_current_configuration_result() {
        assert_value_round_trips_as(
            QueryCurrentConfigurationResult::NotSupported,
            json!("not_supported"),
        );
        assert_value_round_trips_as(
            QueryCurrentConfigurationResult::Success(Configuration::A),
            json!({"success": "a"}),
        );
    }

    #[test]
    fn serde_query_configuration_status_result() {
        assert_value_round_trips_as(
            QueryConfigurationStatusResult::NotSupported,
            json!("not_supported"),
        );
        assert_value_round_trips_as(
            QueryConfigurationStatusResult::Success(ConfigurationStatus::Healthy),
            json!({"success": "healthy"}),
        );
    }

    #[test]
    fn serde_query_configuration_request() {
        assert_value_round_trips_as(
            QueryConfigurationStatusRequest { configuration: Configuration::Recovery },
            json!({"configuration": "recovery"}),
        );
    }

    #[test]
    fn serde_read_asset_request() {
        assert_value_round_trips_as(
            ReadAssetRequest {
                configuration: Configuration::A,
                asset: Asset::VerifiedBootMetadata,
            },
            json!({"configuration": "a", "asset": "verified_boot_metadata"}),
        );
    }

    struct MockBootManagerBuilder {
        expected: Vec<Box<dyn FnOnce(BootManagerRequest) + Send + 'static>>,
    }

    impl MockBootManagerBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(BootManagerRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_query_active_configuration(self, res: Result<Configuration, Status>) -> Self {
            self.push(move |req| match req {
                BootManagerRequest::QueryActiveConfiguration { responder } => {
                    responder.send(&mut res.map(Into::into).map_err(|e| e.into_raw())).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_query_current_configuration(self, res: Result<Configuration, Status>) -> Self {
            self.push(move |req| match req {
                BootManagerRequest::QueryCurrentConfiguration { responder } => {
                    responder.send(&mut res.map(Into::into).map_err(|e| e.into_raw())).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_query_configuration_status(
            self,
            config: Configuration,
            res: Result<ConfigurationStatus, Status>,
        ) -> Self {
            self.push(move |req| match req {
                BootManagerRequest::QueryConfigurationStatus { configuration, responder } => {
                    assert_eq!(Configuration::from(configuration), config);
                    responder.send(&mut res.map(Into::into).map_err(|e| e.into_raw())).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self, mut stream: BootManagerRequestStream) -> impl Future<Output = ()> {
            async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            }
        }
    }

    struct MockDataSinkBuilder {
        expected: Vec<Box<dyn FnOnce(DataSinkRequest) + Send + 'static>>,
    }

    impl MockDataSinkBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(DataSinkRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_read_asset(
            self,
            expected_request: ReadAssetRequest,
            response: &'static [u8],
        ) -> Self {
            let buf = fidl_fuchsia_mem::Buffer {
                vmo: fuchsia_zircon::Vmo::create(response.len() as u64).unwrap(),
                size: response.len() as u64,
            };
            buf.vmo.write(response, 0).unwrap();

            self.push(move |req| match req {
                DataSinkRequest::ReadAsset { configuration, asset, responder } => {
                    let request = ReadAssetRequest {
                        configuration: configuration.into(),
                        asset: asset.into(),
                    };
                    assert_eq!(request, expected_request);

                    responder.send(&mut Ok(buf)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self, mut stream: DataSinkRequestStream) -> impl Future<Output = ()> {
            async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            }
        }
    }

    struct MockPaverBuilder {
        expected: Vec<Box<dyn FnOnce(PaverRequest) + 'static>>,
    }

    impl MockPaverBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(PaverRequest) + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_find_boot_manager(self, mock: Option<MockBootManagerBuilder>) -> Self {
            self.push(move |req| match req {
                PaverRequest::FindBootManager { boot_manager, .. } => {
                    if let Some(mock) = mock {
                        let stream = boot_manager.into_stream().unwrap();
                        fuchsia_async::Task::spawn(async move {
                            mock.build(stream).await;
                        })
                        .detach();
                    } else {
                        boot_manager.close_with_epitaph(Status::NOT_SUPPORTED).unwrap();
                    }
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_find_data_sink(self, mock: MockDataSinkBuilder) -> Self {
            self.push(move |req| match req {
                PaverRequest::FindDataSink { data_sink, .. } => {
                    let stream = data_sink.into_stream().unwrap();
                    fuchsia_async::Task::spawn(async move {
                        mock.build(stream).await;
                    })
                    .detach();
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (PaverFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<PaverMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (PaverFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_active_configuration_ok() {
        let (facade, paver) = MockPaverBuilder::new()
            .expect_find_boot_manager(Some(
                MockBootManagerBuilder::new()
                    .expect_query_active_configuration(Ok(Configuration::A)),
            ))
            .expect_find_boot_manager(Some(
                MockBootManagerBuilder::new()
                    .expect_query_active_configuration(Ok(Configuration::B)),
            ))
            .build();

        let test = async move {
            assert_matches!(
                facade.query_active_configuration().await,
                Ok(QueryActiveConfigurationResult::Success(Configuration::A))
            );
            assert_matches!(
                facade.query_active_configuration().await,
                Ok(QueryActiveConfigurationResult::Success(Configuration::B))
            );
        };

        join!(paver, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_active_configuration_not_supported() {
        let (facade, paver) = MockPaverBuilder::new().expect_find_boot_manager(None).build();

        let test = async move {
            assert_matches!(
                facade.query_active_configuration().await,
                Ok(QueryActiveConfigurationResult::NotSupported)
            );
        };

        join!(paver, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_current_configuration_ok() {
        let (facade, paver) = MockPaverBuilder::new()
            .expect_find_boot_manager(Some(
                MockBootManagerBuilder::new()
                    .expect_query_current_configuration(Ok(Configuration::A)),
            ))
            .expect_find_boot_manager(Some(
                MockBootManagerBuilder::new()
                    .expect_query_current_configuration(Ok(Configuration::B)),
            ))
            .build();

        let test = async move {
            assert_matches!(
                facade.query_current_configuration().await,
                Ok(QueryCurrentConfigurationResult::Success(Configuration::A))
            );
            assert_matches!(
                facade.query_current_configuration().await,
                Ok(QueryCurrentConfigurationResult::Success(Configuration::B))
            );
        };

        join!(paver, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_current_configuration_not_supported() {
        let (facade, paver) = MockPaverBuilder::new().expect_find_boot_manager(None).build();

        let test = async move {
            assert_matches!(
                facade.query_current_configuration().await,
                Ok(QueryCurrentConfigurationResult::NotSupported)
            );
        };

        join!(paver, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_configuration_status_ok() {
        let (facade, paver) = MockPaverBuilder::new()
            .expect_find_boot_manager(Some(
                MockBootManagerBuilder::new().expect_query_configuration_status(
                    Configuration::A,
                    Ok(ConfigurationStatus::Healthy),
                ),
            ))
            .expect_find_boot_manager(Some(
                MockBootManagerBuilder::new().expect_query_configuration_status(
                    Configuration::B,
                    Ok(ConfigurationStatus::Unbootable),
                ),
            ))
            .build();

        let test = async move {
            assert_matches!(
                facade
                    .query_configuration_status(QueryConfigurationStatusRequest {
                        configuration: Configuration::A
                    })
                    .await,
                Ok(QueryConfigurationStatusResult::Success(ConfigurationStatus::Healthy))
            );
            assert_matches!(
                facade
                    .query_configuration_status(QueryConfigurationStatusRequest {
                        configuration: Configuration::B
                    })
                    .await,
                Ok(QueryConfigurationStatusResult::Success(ConfigurationStatus::Unbootable))
            );
        };

        join!(paver, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn query_configuration_status_not_supported() {
        let (facade, paver) = MockPaverBuilder::new().expect_find_boot_manager(None).build();

        let test = async move {
            assert_matches!(
                facade
                    .query_configuration_status(QueryConfigurationStatusRequest {
                        configuration: Configuration::A
                    })
                    .await,
                Ok(QueryConfigurationStatusResult::NotSupported)
            );
        };

        join!(paver, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_asset_ok() {
        const FILE_CONTENTS: &[u8] = b"hello world!";
        const FILE_CONTENTS_AS_BASE64: &str = "aGVsbG8gd29ybGQh";

        let request = ReadAssetRequest {
            configuration: Configuration::A,
            asset: Asset::VerifiedBootMetadata,
        };

        let (facade, paver) = MockPaverBuilder::new()
            .expect_find_data_sink(
                MockDataSinkBuilder::new().expect_read_asset(request.clone(), FILE_CONTENTS),
            )
            .build();

        let test = async move {
            assert_matches!(
                facade.read_asset(request).await,
                Ok(s) if s == FILE_CONTENTS_AS_BASE64
            );
        };

        join!(paver, test);
    }
}
