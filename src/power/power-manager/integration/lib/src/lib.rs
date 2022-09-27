// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mocks;

use {
    crate::mocks::{
        activity_service::MockActivityService, driver_manager::MockDriverManager,
        input_settings_service::MockInputSettingsService,
        system_controller::MockSystemControllerService, temperature_driver::MockTemperatureDriver,
    },
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl::AsHandleRef as _,
    fidl_fuchsia_testing as ftesting, fidl_fuchsia_thermal as fthermal,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    std::collections::HashMap,
    std::sync::atomic::{AtomicU64, Ordering},
    std::sync::Arc,
    tracing::*,
};

const POWER_MANAGER_URL: &str = "#meta/power-manager.cm";
const MOCK_COBALT_URL: &str = "#meta/mock_cobalt.cm";
const FAKE_CLOCK_URL: &str = "#meta/fake_clock.cm";

/// Increase the time scale so Power Manager's interval-based operation runs faster for testing.
const FAKE_TIME_SCALE: u32 = 100;

/// Unique number that is incremented for each TestEnv to avoid name clashes.
static UNIQUE_REALM_NUMBER: AtomicU64 = AtomicU64::new(0);

pub struct TestEnvBuilder {
    power_manager_node_config_path: Option<String>,
    temperature_driver_paths: Vec<String>,
}

impl TestEnvBuilder {
    pub fn new() -> Self {
        Self { power_manager_node_config_path: None, temperature_driver_paths: Vec::new() }
    }

    /// Sets the node config path that Power Manager will be configured with.
    pub fn power_manager_node_config_path(mut self, path: &str) -> Self {
        self.power_manager_node_config_path = Some(path.into());
        self
    }

    /// Sets the temperature driver paths to be added to the mock devfs. A MockTemperatureDriver is
    /// created for each entry.
    pub fn temperature_driver_paths(mut self, paths: Vec<&str>) -> Self {
        self.temperature_driver_paths = paths.into_iter().map(String::from).collect();
        self
    }

    pub async fn build(self) -> TestEnv {
        let realm_builder = RealmBuilder::new().await.expect("Failed to create RealmBuilder");

        let power_manager = realm_builder
            .add_child("power_manager", POWER_MANAGER_URL, ChildOptions::new())
            .await
            .expect("Failed to add child: power_manager");

        let mock_cobalt = realm_builder
            .add_child("mock_cobalt", MOCK_COBALT_URL, ChildOptions::new())
            .await
            .expect("Failed to add child: mock_cobalt");

        let fake_clock = realm_builder
            .add_child("fake_clock", FAKE_CLOCK_URL, ChildOptions::new())
            .await
            .expect("Failed to add child: fake_clock");

        let activity_service = MockActivityService::new();
        let activity_service_clone = activity_service.clone();
        let activity_service_child = realm_builder
            .add_local_child(
                "activity_service",
                move |handles| Box::pin(activity_service_clone.clone().run(handles)),
                ChildOptions::new(),
            )
            .await
            .expect("Failed to add child: activity_service");

        let driver_manager = MockDriverManager::new();
        let driver_manager_clone = driver_manager.clone();
        let driver_manager_child = realm_builder
            .add_local_child(
                "driver_manager",
                move |handles| Box::pin(driver_manager_clone.clone().run(handles)),
                ChildOptions::new().eager(),
            )
            .await
            .expect("Failed to add child: driver_manager");

        let temperature_mocks = self
            .temperature_driver_paths
            .iter()
            .map(|path| {
                let temperature_mock = MockTemperatureDriver::new(path);
                driver_manager.add_temperature_mock(path, temperature_mock.clone());
                (path.into(), temperature_mock)
            })
            .collect();

        let input_settings_service = MockInputSettingsService::new();
        let input_settings_service_clone = input_settings_service.clone();
        let input_settings_service_child = realm_builder
            .add_local_child(
                "input_settings_service",
                move |handles| Box::pin(input_settings_service_clone.clone().run(handles)),
                ChildOptions::new(),
            )
            .await
            .expect("Failed to add child: input_settings_service");

        let system_controller_service = MockSystemControllerService::new();
        let system_controller_service_clone = system_controller_service.clone();
        let system_controller_service_child = realm_builder
            .add_local_child(
                "system_controller_service",
                move |handles| Box::pin(system_controller_service_clone.clone().run(handles)),
                ChildOptions::new(),
            )
            .await
            .expect("Failed to add child: system_controller_service");

        // Set up Power Manager's required routes
        let parent_to_power_manager_routes = Route::new()
            .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
            .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
            .capability(Capability::protocol_by_name("fuchsia.boot.WriteOnlyLog"));
        realm_builder
            .add_route(parent_to_power_manager_routes.from(Ref::parent()).to(&power_manager))
            .await
            .unwrap();

        let parent_to_cobalt_routes =
            Route::new().capability(Capability::protocol_by_name("fuchsia.logger.LogSink"));
        realm_builder
            .add_route(parent_to_cobalt_routes.from(Ref::parent()).to(&mock_cobalt))
            .await
            .unwrap();

        let parent_to_fake_clock_routes =
            Route::new().capability(Capability::protocol_by_name("fuchsia.logger.LogSink"));
        realm_builder
            .add_route(parent_to_fake_clock_routes.from(Ref::parent()).to(&fake_clock))
            .await
            .unwrap();

        let fake_clock_to_power_manager_routes =
            Route::new().capability(Capability::protocol_by_name("fuchsia.testing.FakeClock"));
        realm_builder
            .add_route(fake_clock_to_power_manager_routes.from(&fake_clock).to(&power_manager))
            .await
            .unwrap();

        let fake_clock_to_parent_routes = Route::new()
            .capability(Capability::protocol_by_name("fuchsia.testing.FakeClockControl"));
        realm_builder
            .add_route(fake_clock_to_parent_routes.from(&fake_clock).to(Ref::parent()))
            .await
            .unwrap();

        let cobalt_to_power_manager_routes = Route::new()
            .capability(Capability::protocol_by_name("fuchsia.metrics.MetricEventLoggerFactory"));
        realm_builder
            .add_route(cobalt_to_power_manager_routes.from(&mock_cobalt).to(&power_manager))
            .await
            .unwrap();

        let activity_service_to_power_manager_routes =
            Route::new().capability(Capability::protocol_by_name("fuchsia.ui.activity.Provider"));
        realm_builder
            .add_route(
                activity_service_to_power_manager_routes
                    .from(&activity_service_child)
                    .to(&power_manager),
            )
            .await
            .unwrap();

        let input_settings_service_to_power_manager_routes =
            Route::new().capability(Capability::protocol_by_name("fuchsia.settings.Input"));
        realm_builder
            .add_route(
                input_settings_service_to_power_manager_routes
                    .from(&input_settings_service_child)
                    .to(&power_manager),
            )
            .await
            .unwrap();

        let system_controller_to_power_manager_routes =
            Route::new().capability(Capability::protocol_by_name("fuchsia.sys2.SystemController"));
        realm_builder
            .add_route(
                system_controller_to_power_manager_routes
                    .from(&system_controller_service_child)
                    .to(&power_manager),
            )
            .await
            .unwrap();

        let power_manager_to_parent_routes = Route::new()
            .capability(Capability::protocol_by_name("fuchsia.thermal.ClientStateConnector"));
        realm_builder
            .add_route(power_manager_to_parent_routes.from(&power_manager).to(Ref::parent()))
            .await
            .unwrap();

        let power_manager_to_driver_manager = Route::new().capability(
            Capability::protocol_by_name("fuchsia.power.manager.DriverManagerRegistration"),
        );
        realm_builder
            .add_route(
                power_manager_to_driver_manager.from(&power_manager).to(&driver_manager_child),
            )
            .await
            .unwrap();

        // Update Power Manager's structured config values
        realm_builder.init_mutable_config_from_package(&power_manager).await.unwrap();
        realm_builder
            .set_config_value_string(
                &power_manager,
                "node_config_path",
                self.power_manager_node_config_path
                    .as_ref()
                    .expect("power_manager_node_config_path not set"),
            )
            .await
            .unwrap();

        // Generate a unique realm name based on the current process ID and unique realm number for
        // the current process.
        let realm_name = format!(
            "{}-{}",
            fuchsia_runtime::process_self().get_koid().unwrap().raw_koid(),
            UNIQUE_REALM_NUMBER.fetch_add(1, Ordering::Relaxed)
        );

        // Finally, build it
        let realm_instance =
            realm_builder.build_with_name(realm_name).await.expect("Failed to build RealmInstance");

        // Increase the time scale so Power Manager's interval-based operation runs faster for
        // testing
        set_fake_time_scale(&realm_instance, FAKE_TIME_SCALE).await;

        TestEnv {
            realm_instance: Some(realm_instance),
            mocks: Mocks {
                activity_service,
                driver_manager,
                input_settings_service,
                temperature: temperature_mocks,
                system_controller_service,
            },
        }
    }
}

pub struct TestEnv {
    realm_instance: Option<RealmInstance>,
    pub mocks: Mocks,
}

impl TestEnv {
    /// Connects to a protocol exposed by a component within the RealmInstance.
    pub fn connect_to_protocol<P: DiscoverableProtocolMarker>(&self) -> P::Proxy {
        self.realm_instance
            .as_ref()
            .unwrap()
            .root
            .connect_to_protocol_at_exposed_dir::<P>()
            .unwrap()
    }

    /// Destroys the TestEnv and underlying RealmInstance.
    ///
    /// Every test that uses TestEnv must call this at the end of the test.
    pub async fn destroy(&mut self) {
        info!("Destroying TestEnv");
        self.realm_instance
            .take()
            .expect("Missing realm instance")
            .destroy()
            .await
            .expect("Failed to destroy realm instance");
    }

    /// Sets the temperature for a mock temperature device.
    pub fn set_temperature(&self, driver_path: &str, temperature: f32) {
        self.mocks.temperature[driver_path].set_temperature(temperature);
    }
}

/// Ensures `destroy` was called on the TestEnv prior to it going out of scope. It would be nice to
/// do the work of `destroy` right here in `drop`, but we can't since `destroy` requires async.
impl Drop for TestEnv {
    fn drop(&mut self) {
        assert!(self.realm_instance.is_none(), "Must call destroy() to tear down test environment");
    }
}

/// Increases the time scale so Power Manager's interval-based operation runs faster for testing.
async fn set_fake_time_scale(realm_instance: &RealmInstance, scale: u32) {
    let fake_clock_control = realm_instance
        .root
        .connect_to_protocol_at_exposed_dir::<ftesting::FakeClockControlMarker>()
        .unwrap();

    fake_clock_control.pause().await.expect("failed to pause fake time: FIDL error");
    fake_clock_control
        .resume_with_increments(
            fuchsia_zircon::Duration::from_millis(1).into_nanos(),
            &mut ftesting::Increment::Determined(
                fuchsia_zircon::Duration::from_millis(scale.into()).into_nanos(),
            ),
        )
        .await
        .expect("failed to set fake time scale: FIDL error")
        .expect("failed to set fake time scale: protocol error");
}

/// Container to hold all of the mocks within the RealmInstance.
pub struct Mocks {
    pub activity_service: Arc<MockActivityService>,
    pub driver_manager: Arc<MockDriverManager>,
    pub input_settings_service: Arc<MockInputSettingsService>,
    pub system_controller_service: Arc<MockSystemControllerService>,
    pub temperature: HashMap<String, Arc<MockTemperatureDriver>>,
}

/// Convenience type for interacting with the Power Manager's thermal client service.
pub struct ThermalClient {
    watcher_proxy: fthermal::ClientStateWatcherProxy,
}

impl ThermalClient {
    pub fn new(test_env: &TestEnv, client_type: &str) -> Self {
        let connector = test_env.connect_to_protocol::<fthermal::ClientStateConnectorMarker>();
        let (watcher_proxy, watcher_remote) =
            fidl::endpoints::create_proxy::<fthermal::ClientStateWatcherMarker>().unwrap();
        connector.connect(client_type, watcher_remote).expect("Failed to connect thermal client");
        Self { watcher_proxy }
    }

    pub async fn get_thermal_state(&mut self) -> Result<u64, anyhow::Error> {
        Ok(self.watcher_proxy.watch().await?)
    }
}
