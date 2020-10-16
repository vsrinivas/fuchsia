// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_examples_intl_manager::{PropertyManagerRequest, PropertyManagerRequestStream},
    fidl_fuchsia_intl::{
        LocaleId, Profile, PropertyProviderControlHandle, PropertyProviderRequest,
        PropertyProviderRequestStream, TimeZoneId,
    },
    fidl_fuchsia_intl_ext::CloneExt,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObjLocal},
    fuchsia_syslog::{self, fx_log_err, fx_log_info},
    futures::{self, lock::Mutex, prelude::*},
    std::{
        collections::HashMap,
        fmt::{self, Debug},
        rc::Rc,
        sync::{Arc, RwLock},
    },
    structopt::StructOpt,
};

static LOG_TAG: &str = "intl_property_manager";
static LOG_VERBOSITY: u16 = 1;

/// Convenience wrapper around `fx_vlog`, with a pre-set verbosity.
macro_rules! fx_log_verbose {
    ($($arg:tt)*) => {
        fuchsia_syslog::fx_vlog!(LOG_VERBOSITY as i32, $($arg)*);
    };
}

#[derive(Clone)]
struct Server(Arc<ServerState>);

struct ServerState {
    /// Current profile being served.
    profile: RwLock<Option<Profile>>,
    /// Listeners of `OnChange` events.
    listeners: Mutex<PropertyProviderListeners>,
}

#[derive(PartialEq, Eq, Hash, Clone, Copy)]
struct PropertyProviderListenerKey(usize);

struct PropertyProviderListeners {
    collection: HashMap<PropertyProviderListenerKey, PropertyProviderControlHandle>,
    counter: usize,
}

impl PropertyProviderListeners {
    fn new() -> PropertyProviderListeners {
        PropertyProviderListeners { collection: HashMap::new(), counter: 0 }
    }

    fn add(&mut self, listener: PropertyProviderControlHandle) -> PropertyProviderListenerKey {
        let key = PropertyProviderListenerKey(self.counter);
        self.collection.insert(key, listener);
        self.counter += 1;
        key
    }

    fn remove(&mut self, key: PropertyProviderListenerKey) {
        self.collection.remove(&key);
    }

    fn notify(&mut self) {
        // Prune any listeners for which sending the event fails. This means they have
        // disconnected.
        self.collection.retain(|_, control_handle| match control_handle.send_on_change() {
            Ok(_) => true,
            Err(_) => false,
        })
    }
}

impl Server {
    fn new(initial_profile: Option<Profile>) -> Self {
        Server(Arc::new(ServerState {
            profile: RwLock::new(initial_profile),
            listeners: Mutex::new(PropertyProviderListeners::new()),
        }))
    }

    /// Atomically set the profile being served. Returns `true` if the value is changed.
    fn set_profile(&mut self, new_profile: Profile) -> bool {
        let mut p = self.0.profile.write().unwrap();
        let changed = match p.as_ref() {
            Some(stored_profile) => (stored_profile != &new_profile),
            None => true,
        };
        *p = Some(new_profile);
        changed
    }

    /// Get a clone of the current `Profile`, or `None` if the profile hasn't been initialized.
    fn get_profile(&self) -> Option<Profile> {
        let p = self.0.profile.read().unwrap();
        p.as_ref().map(CloneExt::clone)
    }

    /// Register a new listener for profile change events. Note that `PropertyProviderControlHandle`
    /// contains an `Arc` and should be cloned before being passed to `add_listener`.
    ///
    /// Returns a listener key.
    async fn add_listener(
        &mut self,
        listener: PropertyProviderControlHandle,
    ) -> PropertyProviderListenerKey {
        let fut = self.0.listeners.lock().map(|mut listeners| listeners.add(listener));
        fut.await
    }

    /// Remove a registered listener by key.
    async fn remove_listener(&mut self, key: PropertyProviderListenerKey) {
        let fut = self.0.listeners.lock().map(|mut listeners| listeners.remove(key));
        fut.await
    }

    /// Send `OnChange` event to registered listeners of `PropertyProvider`.
    async fn notify_listeners(&mut self) {
        fx_log_verbose!("Notifying listeners");
        let fut = self.0.listeners.lock().map(|mut listeners| listeners.notify());
        fut.await;
        fx_log_verbose!("Notified listeners");
    }

    /// Entry point into the service. Register handlers for both of the protocols
    /// (`PropertyProvider`, `PropertyManager`).
    async fn run(&mut self, fs: ServiceFs<ServiceObjLocal<'static, Service>>) {
        let self_ = Rc::new(self.clone());
        fs.for_each_concurrent(None, move |service| self_.clone().handle_service_stream(service))
            .await;
        fx_log_verbose!("Registered services");
    }

    async fn handle_service_stream(self: Rc<Self>, service: Service) {
        fx_log_verbose!("handle_service_stream: {:#?}", service);
        let mut self_ = self.as_ref().clone();
        match service {
            Service::Provider(stream) => self_.run_provider(stream).await.unwrap_or_default(),
            Service::Manager(stream) => self_.run_manager(stream).await.unwrap_or_default(),
        }
    }

    /// Handle `PropertyProvider` requests as an infinite stream.
    async fn run_provider(
        &mut self,
        mut stream: PropertyProviderRequestStream,
    ) -> Result<(), Error> {
        let listener_key = self.add_listener(stream.control_handle().clone()).await;

        while let Some(PropertyProviderRequest::GetProfile { responder }) =
            stream.try_next().await.context("Error running property provider server")?
        {
            {
                fx_log_verbose!("Received profile get request");
                match self.get_profile() {
                    Some(profile) => {
                        responder.send(profile).context("Error sending response")?;
                        fx_log_verbose!("Sent profile");
                    }
                    None => {
                        fx_log_err!("Profile not initialized");
                        responder
                            .send(Profile {
                                locales: None,
                                time_zones: None,
                                calendars: None,
                                temperature_unit: None,
                            })
                            .context("Error sending response")?;
                        fx_log_verbose!("Sent empty profile");
                    }
                }
            }
        }

        // Don't leak listeners after they disconnect.
        self.remove_listener(listener_key).await;

        Ok(())
    }

    /// Handle `PropertyManager` requests as an infinite stream.
    async fn run_manager(&mut self, mut stream: PropertyManagerRequestStream) -> Result<(), Error> {
        while let Some(PropertyManagerRequest::SetProfile { intl_profile, responder }) =
            stream.try_next().await.context("Error running property manager server")?
        {
            fx_log_verbose!("Received profile set request: {:#?}", &intl_profile);
            let changed = self.set_profile(intl_profile);
            responder.send().context("Error sending response")?;
            fx_log_verbose!("Sent profile set response");
            if changed {
                self.notify_listeners().await;
            }
        }
        Ok(())
    }
}

enum Service {
    Provider(PropertyProviderRequestStream),
    Manager(PropertyManagerRequestStream),
}

/// Manual implementation because `__RequestStream` doesn't implement `Debug`.
impl Debug for Service {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                Service::Provider(_) => "Provider",
                Service::Manager(_) => "Manager",
            }
        )
    }
}

#[derive(Debug, StructOpt)]
#[structopt(
    name = "intl property manager example",
    about = "provides a test implementation of fuchsia.intl.ProfileProvider"
)]
struct Opts {
    #[structopt(long)]
    /// If set to `true`, the starting profile will be created based on the
    /// flag settings like `--locale_ids=...`.
    set_initial_profile: bool,
    #[structopt(long, raw(use_delimiter = "true"))]
    /// A list of comma-separated BCP-47 locale ID strings to serve initially, in the order of
    /// priority.
    locale_ids: Vec<String>,
    #[structopt(long, raw(use_delimiter = "true"))]
    /// A list of comma-separated BCP-47 timezone IDs (e.g. und-tz-usnyc) to serve initially, in
    /// order of preference.
    timezone_ids: Vec<String>,
}
impl From<&Opts> for Profile {
    fn from(opts: &Opts) -> Self {
        Profile {
            locales: Some(
                opts.locale_ids
                    .iter()
                    .map(|loc_id| LocaleId { id: String::from(loc_id) })
                    .collect(),
            ),
            time_zones: Some(
                opts.timezone_ids
                    .iter()
                    .map(|tz_id| TimeZoneId { id: String::from(tz_id) })
                    .collect(),
            ),
            // TODO(fmil): Implement these too.
            calendars: None,
            temperature_unit: None,
        }
    }
}

fn initial_profile(opts: &Opts) -> Option<Profile> {
    // Not sure if it is possible to evict the set_initial flag altogether.
    match opts.set_initial_profile {
        false => None,
        true => {
            let profile = opts.into();
            fx_log_info!("Serving initial profile: {:?}", profile);
            Some(profile)
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opts = Opts::from_args();

    fuchsia_syslog::init_with_tags(&[LOG_TAG])?;
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);

    fx_log_info!("Launched component");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(Service::Provider).add_fidl_service(Service::Manager);
    fs.take_and_serve_directory_handle()?;
    let fs = fs;

    let mut server = Server::new(initial_profile(&opts));
    fx_log_info!("Starting server...");
    server.run(fs).await;
    Ok(())
}

#[cfg(test)]
mod test {
    use {
        anyhow::{Context as _, Error},
        fidl_fuchsia_examples_intl_manager::{PropertyManagerMarker, PropertyManagerProxy},
        fidl_fuchsia_intl::{
            CalendarId, LocaleId, Profile, PropertyProviderEventStream, PropertyProviderMarker,
            PropertyProviderProxy, TemperatureUnit, TimeZoneId,
        },
        fidl_fuchsia_intl_ext::CloneExt,
        fidl_fuchsia_sys::LauncherProxy,
        fuchsia_async as fasync,
        fuchsia_component::client,
        futures::{self, prelude::*},
        lazy_static::lazy_static,
    };

    static COMPONENT_URL: &str =
        "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager.cmx";
    static COMPONENT_URL_WITHOUT_FLAGS: &str =
        "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager_without_flags.cmx";

    lazy_static! {
        static ref PROFILE_EMPTY: Profile =
            Profile { locales: None, calendars: None, time_zones: None, temperature_unit: None };
        static ref PROFILE_A: Profile = Profile {
            locales: Some(vec![
                LocaleId { id: "en-US".to_string() },
                LocaleId { id: "fr-CA".to_string() }
            ]),
            calendars: Some(vec![CalendarId { id: "gregorian".to_string() }]),
            time_zones: Some(vec![TimeZoneId { id: "America/New_York".to_string() }]),
            temperature_unit: Some(TemperatureUnit::Celsius),
        };
        static ref PROFILE_B: Profile = Profile {
            locales: Some(vec![
                LocaleId { id: "ar-EG".to_string() },
                LocaleId { id: "el-GR".to_string() }
            ]),
            calendars: Some(vec![CalendarId { id: "gregorian".to_string() }]),
            time_zones: Some(vec![TimeZoneId { id: "Europe/Athens".to_string() }]),
            temperature_unit: Some(TemperatureUnit::Celsius),
        };
        // This profile corresponds to the flag settings in the manifest at `COMPONENT_URL`.
        static ref INITIAL_PROFILE: Profile = Profile {
            locales: Some(vec![
                LocaleId { id: "en-US".to_string() },
                LocaleId { id: "nl-NL".to_string() }
            ]),
            calendars: None,
            time_zones: Some(vec![TimeZoneId { id: "und-u-tz-uslax".to_string() }]),
            temperature_unit: None,
        };
    }

    /// The test launches the provider/manager, then sets and gets `Profile` values several times,
    /// confirming that `OnChange` events are sent and the updated values are correct.
    #[fasync::run_singlethreaded]
    #[test]
    async fn test_get_set_profile() -> Result<(), Error> {
        let launcher: LauncherProxy =
            client::launcher().context("Failed to open launcher service")?;
        let app = client::launch(&launcher, COMPONENT_URL_WITHOUT_FLAGS.to_string(), None)
            .context("Failed to launch Intl Property Manager")?;

        let property_manager: PropertyManagerProxy = app
            .connect_to_service::<PropertyManagerMarker>()
            .context("Failed to connect to intl PropertyManager service")?;

        let property_provider: PropertyProviderProxy = app
            .connect_to_service::<PropertyProviderMarker>()
            .context("Failed to connect to intl PropertyProvider service")?;

        let initial_profile = property_provider.get_profile().await?;
        assert_eq!(initial_profile, *PROFILE_EMPTY);

        let mut event_stream: PropertyProviderEventStream = property_provider.take_event_stream();

        property_manager.set_profile(PROFILE_A.clone()).await?;
        let event_a_msg = "Failed to get event for PROFILE_A";
        event_stream.next().await.expect(event_a_msg).expect(event_a_msg);
        let actual = property_provider.get_profile().await?;
        assert_eq!(actual, *PROFILE_A);

        property_manager.set_profile(PROFILE_B.clone()).await?;
        let event_b_msg = "Failed to get event for PROFILE_B";
        event_stream.next().await.expect(event_b_msg).expect(event_b_msg);
        let actual = property_provider.get_profile().await?;
        assert_eq!(actual, *PROFILE_B);

        Ok(())
    }

    /// This test confirms that the provider will serve a nonempty initial
    /// profile when invoked from the default component manifest URL, and that
    /// the served profile corresponds to the settings that are currently in
    /// the manifest.
    #[fasync::run_singlethreaded]
    #[test]
    async fn test_set_initial_profile() -> Result<(), Error> {
        let launcher: LauncherProxy =
            client::launcher().context("Failed to open launcher service")?;
        let app = client::launch(&launcher, COMPONENT_URL.to_string(), None)
            .context("Failed to launch Intl Property Manager")?;

        let property_provider: PropertyProviderProxy = app
            .connect_to_service::<PropertyProviderMarker>()
            .context("Failed to connect to intl PropertyProvider service")?;

        let initial_profile = property_provider.get_profile().await?;
        assert_eq!(initial_profile, *INITIAL_PROFILE);

        Ok(())
    }
}
