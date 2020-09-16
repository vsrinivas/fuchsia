// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(51770): move everything except installer into a shared crate,
// because these modules all come from //src/sys/pkg/bin/omaha-client.
mod http_request;
mod install_plan;
mod installer;
mod timer;
use {
    crate::{
        cache::Cache,
        omaha::{
            http_request::FuchsiaHyperHttpRequest, installer::IsolatedInstaller,
            timer::FuchsiaTimer,
        },
        resolver::Resolver,
        updater::UPDATER_URL,
    },
    anyhow::{anyhow, Error},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    futures::lock::Mutex,
    futures::prelude::*,
    log::error,
    omaha_client::{
        common::{App, AppSet},
        configuration::{Config, Updater},
        http_request::HttpRequest,
        metrics::StubMetricsReporter,
        policy::StubPolicyEngine,
        protocol::{request::OS, Cohort},
        state_machine::{update_check, StateMachineBuilder, StateMachineEvent, UpdateCheckError},
        storage::MemStorage,
        time::StandardTimeSource,
    },
    std::{rc::Rc, sync::Arc},
    version::Version,
};

/// Get a |Config| object to use when making requests to Omaha.
async fn get_omaha_config(version: &str, service_url: &str) -> Config {
    Config {
        updater: Updater { name: "Fuchsia".to_string(), version: Version::from([0, 0, 1, 0]) },

        os: OS {
            platform: "Fuchsia".to_string(),
            version: version.to_string(),
            service_pack: "".to_string(),
            arch: std::env::consts::ARCH.to_string(),
        },

        service_url: service_url.to_owned(),
    }
}

/// Get the update URL to use from Omaha, and install the update.
pub async fn install_update(
    blobfs: ClientEnd<DirectoryMarker>,
    paver: ClientEnd<DirectoryMarker>,
    cache: Arc<Cache>,
    resolver: Arc<Resolver>,
    board_name: String,
    appid: String,
    service_url: String,
    current_version: String,
    channel: String,
) -> Result<(), Error> {
    let version = match current_version.parse::<Version>() {
        Ok(version) => version,
        Err(e) => {
            error!("Unable to parse '{}' as Omaha version format: {:?}", current_version, e);
            Version::from([0])
        }
    };

    let cohort = Cohort { hint: Some(channel.clone()), name: Some(channel), ..Cohort::default() };
    let app_set = AppSet::new(vec![App::builder(appid, version).with_cohort(cohort).build()]);

    let config = get_omaha_config(&current_version, &service_url).await;
    install_update_with_http(
        blobfs,
        paver,
        cache,
        resolver,
        board_name,
        app_set,
        config,
        UPDATER_URL.to_owned(),
        FuchsiaHyperHttpRequest::new(),
    )
    .await
}

async fn install_update_with_http<HR>(
    blobfs: ClientEnd<DirectoryMarker>,
    paver: ClientEnd<DirectoryMarker>,
    cache: Arc<Cache>,
    resolver: Arc<Resolver>,
    board_name: String,
    app_set: AppSet,
    config: Config,
    updater_url: String,
    http_request: HR,
) -> Result<(), Error>
where
    HR: HttpRequest,
{
    let storage = Rc::new(Mutex::new(MemStorage::new()));
    let installer =
        IsolatedInstaller::new(blobfs, paver, cache, resolver, board_name.to_owned(), updater_url);
    let state_machine = StateMachineBuilder::new(
        StubPolicyEngine::new(StandardTimeSource),
        http_request,
        installer,
        FuchsiaTimer,
        StubMetricsReporter,
        storage,
        config.clone(),
        app_set.clone(),
    );

    let stream: Vec<StateMachineEvent> = state_machine.oneshot_check().await.collect().await;

    let mut result: Vec<Result<update_check::Response, UpdateCheckError>> = stream
        .into_iter()
        .filter_map(|p| match p {
            StateMachineEvent::UpdateCheckResult(val) => Some(val),
            _ => None,
        })
        .collect();
    if result.len() != 1 {
        return Err(anyhow!("Expected exactly one UpdateCheckResult from Omaha"));
    }

    let state = result.pop().unwrap();
    if let Err(e) = state {
        return Err(Error::new(e));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            resolver::for_tests::ResolverForTest,
            updater::for_tests::{UpdaterBuilder, UpdaterForTest, UpdaterResult, TEST_UPDATER_URL},
        },
        anyhow::Context,
        fidl_fuchsia_paver::PaverRequestStream,
        fuchsia_async as fasync,
        fuchsia_component::server::{ServiceFs, ServiceObj},
        fuchsia_pkg_testing::PackageBuilder,
        fuchsia_zircon as zx,
        mock_paver::PaverEvent,
        omaha_client::http_request::mock::MockHttpRequest,
        serde_json::json,
    };

    const TEST_REPO_URL: &str = "fuchsia-pkg://example.com";

    const TEST_VERSION: &str = "20200101.0.0";
    const TEST_CHANNEL: &str = "test-channel";
    const TEST_APP_ID: &str = "qnzHyt4n";

    /// Use the Omaha state machine to perform an update.
    ///
    /// Arguments:
    /// * `updater`: UpdaterForTest environment to use with Omaha.
    /// * `app_set`: AppSet for use by Omaha.
    /// * `config`: Omaha client configuration.
    /// * `mock_responses`: In-order list of responses Omaha should get for each HTTP request it
    ///     makes.
    async fn run_omaha(
        updater: UpdaterForTest,
        app_set: AppSet,
        config: Config,
        mock_responses: Vec<serde_json::Value>,
    ) -> Result<UpdaterResult, Error> {
        let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();
        let paver_clone = Arc::clone(&updater.paver);
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            fasync::Task::spawn(
                Arc::clone(&paver_clone)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("Failed to run mock paver: {:?}", e)),
            )
            .detach();
        });
        let (client, server) = zx::Channel::create().expect("creating channel");
        fs.serve_connection(server).expect("Failed to start mock paver");
        fasync::Task::spawn(fs.collect()).detach();

        let mut http = MockHttpRequest::empty();

        for response in mock_responses {
            let response = serde_json::to_vec(&response).unwrap();
            http.add_response(hyper::Response::new(response.into()));
        }

        let resolver = ResolverForTest::new(updater.repo, TEST_REPO_URL.parse().unwrap(), None)
            .await
            .expect("Creating resolver");

        install_update_with_http(
            resolver.cache.pkgfs.blobfs.root_dir_handle().expect("getting blobfs root handle"),
            ClientEnd::from(client),
            Arc::clone(&resolver.cache.cache),
            Arc::clone(&resolver.resolver),
            "test".to_owned(),
            app_set,
            config,
            TEST_UPDATER_URL.to_owned(),
            http,
        )
        .await
        .context("Running omaha client")?;

        Ok(UpdaterResult {
            paver_events: updater.paver.take_events(),
            resolver,
            output: None,
            packages: updater.packages,
        })
    }

    fn get_test_app_set() -> AppSet {
        AppSet::new(vec![App::builder(TEST_APP_ID.to_owned(), [20200101, 0, 0, 0])
            .with_cohort(Cohort::new(TEST_CHANNEL))
            .build()])
    }

    fn get_test_config() -> Config {
        Config {
            updater: Updater { name: "Fuchsia".to_owned(), version: Version::from([0, 0, 1, 0]) },
            os: OS {
                platform: "Fuchsia".to_owned(),
                version: TEST_VERSION.to_owned(),
                service_pack: "".to_owned(),
                arch: std::env::consts::ARCH.to_owned(),
            },

            // Since we're using the mock http resolver, this doesn't matter.
            service_url: "http://example.com".to_owned(),
        }
    }

    /// Construct an UpdaterForTest for use in the Omaha tests.
    async fn build_updater() -> Result<UpdaterForTest, Error> {
        let data = "hello world!".as_bytes();
        let hook = |p: &PaverEvent| {
            if let PaverEvent::QueryActiveConfiguration = p {
                return zx::Status::NOT_SUPPORTED;
            }
            zx::Status::OK
        };
        let test_package = PackageBuilder::new("test_package")
            .add_resource_at("bin/hello", "this is a test".as_bytes())
            .add_resource_at("data/file", "this is a file".as_bytes())
            .add_resource_at("meta/test_package.cmx", "{}".as_bytes())
            .build()
            .await
            .context("Building test_package")?;
        let updater = UpdaterBuilder::new()
            .await
            .paver(|p| p.call_hook(hook))
            .repo_url(TEST_REPO_URL)
            .add_package(test_package)
            .add_image("zbi.signed", &data)
            .add_image("fuchsia.vbmeta", &data)
            .add_image("zedboot.signed", &data)
            .add_image("recovery.vbmeta", &data);
        let updater = updater.build().await;
        Ok(updater)
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_omaha_update() -> Result<(), Error> {
        // Set up the logger so we get log output from Omaha.
        fuchsia_syslog::init().expect("Failed to init logger");

        let updater = build_updater().await.context("Building updater")?;
        let package_path = format!("update?hash={}", updater.update_merkle_root);
        let update_response = json!({"response":{
            "server": "prod",
            "protocol": "3.0",
            "app": [{
                "appid": TEST_APP_ID,
                "status": "ok",
                "updatecheck": {
                    "status": "ok",
                    "urls": { "url": [{ "codebase": format!("{}/", TEST_REPO_URL) }] },
                    "manifest": {
                        "version": "20200101.1.0.0",
                        "actions": {
                            "action": [
                                {
                                    "run": &package_path,
                                    "event": "install"
                                },
                                {
                                    "event": "postinstall"
                                }
                            ]
                        },
                        "packages": {
                            "package": [
                                {
                                    "name": &package_path,
                                    "fp": "2.20200101.1.0.0",
                                    "required": true,
                                }
                            ]
                        }
                    }
                }
            }],
        }});

        let event_response = json!({"response":{
            "server": "prod",
            "protocol": "3.0",
            "app": [{
                "appid": TEST_APP_ID,
                "status": "ok",
            }]
        }});

        let app_set = get_test_app_set();
        let config = get_test_config();
        let response =
            vec![update_response, event_response.clone(), event_response.clone(), event_response];
        let result =
            run_omaha(updater, app_set, config, response).await.context("running omaha")?;

        result.verify_packages().await.expect("Packages are all there");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_omaha_updater_reports_failure() -> Result<(), Error> {
        // Set up the logger so we get log output from Omaha.
        fuchsia_syslog::init().expect("Failed to init logger");
        let app_set = get_test_app_set();
        let config = get_test_config();
        let updater = build_updater().await.context("Building updater")?;
        let response = vec![];

        let result = run_omaha(updater, app_set, config, response).await;
        assert!(result.is_err());
        Ok(())
    }
}
