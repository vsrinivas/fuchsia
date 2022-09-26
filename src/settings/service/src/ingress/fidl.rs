// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{Dependency, Entity, SettingType};
use crate::handler::base::Error;
use crate::ingress::registration::{self, Registrant, Registrar};
use crate::job::source::Seeder;
use crate::policy::PolicyType;
use fidl_fuchsia_settings::{
    AccessibilityRequestStream, AudioRequestStream, DisplayRequestStream,
    DoNotDisturbRequestStream, FactoryResetRequestStream, InputRequestStream, IntlRequestStream,
    KeyboardRequestStream, LightRequestStream, NightModeRequestStream, PrivacyRequestStream,
    SetupRequestStream,
};
use fidl_fuchsia_settings_policy::VolumePolicyControllerRequestStream;
use fuchsia_component::server::{ServiceFsDir, ServiceObj};
use fuchsia_zircon;
use serde::Deserialize;

impl From<Error> for fuchsia_zircon::Status {
    fn from(error: Error) -> fuchsia_zircon::Status {
        match error {
            Error::UnhandledType(_) => fuchsia_zircon::Status::UNAVAILABLE,
            _ => fuchsia_zircon::Status::INTERNAL,
        }
    }
}
/// [Interface] defines the FIDL interfaces supported by the settings service.
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub enum Interface {
    Accessibility,
    Audio,
    AudioPolicy,
    Display(display::InterfaceFlags),
    DoNotDisturb,
    FactoryReset,
    Input,
    Intl,
    Keyboard,
    Light,
    NightMode,
    Privacy,
    Setup,
}

/// [InterfaceSpec] is the serializable type that defines the configuration for FIDL interfaces
/// supported by the settings service. It's read in from configuration files to modify what
/// interfaces the settings service provides.
#[derive(Clone, Deserialize, PartialEq, Eq, Hash, Debug)]
pub enum InterfaceSpec {
    Accessibility,
    Audio,
    AudioPolicy,
    // Should ideally be a HashSet, but HashSet does not impl Hash.
    Display(Vec<display::InterfaceSpec>),
    DoNotDisturb,
    FactoryReset,
    Input,
    Intl,
    Keyboard,
    Light,
    NightMode,
    Privacy,
    Setup,
}

impl From<InterfaceSpec> for Interface {
    fn from(spec: InterfaceSpec) -> Self {
        match spec {
            InterfaceSpec::Audio => Interface::Audio,
            InterfaceSpec::AudioPolicy => Interface::AudioPolicy,
            InterfaceSpec::Accessibility => Interface::Accessibility,
            InterfaceSpec::Display(variants) => Interface::Display(variants.into()),
            InterfaceSpec::DoNotDisturb => Interface::DoNotDisturb,
            InterfaceSpec::FactoryReset => Interface::FactoryReset,
            InterfaceSpec::Input => Interface::Input,
            InterfaceSpec::Intl => Interface::Intl,
            InterfaceSpec::Keyboard => Interface::Keyboard,
            InterfaceSpec::Light => Interface::Light,
            InterfaceSpec::NightMode => Interface::NightMode,
            InterfaceSpec::Privacy => Interface::Privacy,
            InterfaceSpec::Setup => Interface::Setup,
        }
    }
}

pub mod display {
    use bitflags::bitflags;
    use serde::Deserialize;

    bitflags! {
        /// The Display interface covers a number of feature spaces, each handled by a different
        /// entity dependency. The flags below allow the scope of these features to be specified by
        /// the interface.
        pub struct InterfaceFlags: u64 {
            const BASE = 1 << 0;
            const LIGHT_SENSOR = 1 << 1;
        }
    }

    #[derive(Copy, Clone, Deserialize, PartialEq, Eq, Hash, Debug)]
    pub enum InterfaceSpec {
        Base,
        LightSensor,
    }

    impl From<Vec<InterfaceSpec>> for InterfaceFlags {
        fn from(variants: Vec<InterfaceSpec>) -> Self {
            variants.into_iter().fold(InterfaceFlags::empty(), |flags, variant| {
                flags
                    | match variant {
                        InterfaceSpec::Base => InterfaceFlags::BASE,
                        InterfaceSpec::LightSensor => InterfaceFlags::LIGHT_SENSOR,
                    }
            })
        }
    }
}

/// [Register] defines the closure implemented for interfaces to bring up support. Each interface
/// handler is given access to the MessageHub [Delegate] for communication within the service and
/// [ServiceFsDir] to register as the designated handler for the interface.
pub(crate) type Register =
    Box<dyn for<'a> FnOnce(&Seeder, &mut ServiceFsDir<'_, ServiceObj<'a, ()>>) + Send + Sync>;

impl Interface {
    /// Returns the list of [Dependencies](Dependency) that are necessary to provide this Interface.
    fn dependencies(self) -> Vec<Dependency> {
        match self {
            Interface::Accessibility => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Accessibility))]
            }
            Interface::Audio => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Audio))]
            }
            Interface::AudioPolicy => {
                vec![Dependency::Entity(Entity::PolicyHandler(PolicyType::Audio))]
            }
            Interface::Display(interfaces) => {
                let mut dependencies = Vec::new();

                if interfaces.contains(display::InterfaceFlags::BASE) {
                    dependencies.push(Dependency::Entity(Entity::Handler(SettingType::Display)));
                }

                if interfaces.contains(display::InterfaceFlags::LIGHT_SENSOR) {
                    dependencies
                        .push(Dependency::Entity(Entity::Handler(SettingType::LightSensor)));
                }

                if dependencies.is_empty() {
                    panic!("A valid interface flag must be specified with Interface::Display");
                }

                dependencies
            }
            Interface::DoNotDisturb => {
                vec![Dependency::Entity(Entity::Handler(SettingType::DoNotDisturb))]
            }
            Interface::FactoryReset => {
                vec![Dependency::Entity(Entity::Handler(SettingType::FactoryReset))]
            }
            Interface::Input => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Input))]
            }
            Interface::Intl => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Intl))]
            }
            Interface::Keyboard => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Keyboard))]
            }
            Interface::Light => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Light))]
            }
            Interface::NightMode => {
                vec![Dependency::Entity(Entity::Handler(SettingType::NightMode))]
            }
            Interface::Privacy => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Privacy))]
            }
            Interface::Setup => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Setup))]
            }
        }
    }

    /// Converts an [Interface] into the closure to bring up the interface in the service environment
    /// as defined by [Register].
    fn registration_fn(self) -> Register {
        Box::new(move |seeder: &Seeder, service_dir: &mut ServiceFsDir<'_, ServiceObj<'_, ()>>| {
            match self {
                Interface::Audio => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: AudioRequestStream| {
                        seeder.seed(stream);
                    });
                }
                Interface::AudioPolicy => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(
                        move |stream: VolumePolicyControllerRequestStream| {
                            seeder.seed(stream);
                        },
                    );
                }
                Interface::Accessibility => {
                    let seeder = seeder.clone();
                    let _ =
                        service_dir.add_fidl_service(move |stream: AccessibilityRequestStream| {
                            seeder.seed(stream);
                        });
                }
                Interface::Display(_) => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: DisplayRequestStream| {
                        seeder.seed(stream);
                    });
                }
                Interface::DoNotDisturb => {
                    let seeder = seeder.clone();
                    let _ =
                        service_dir.add_fidl_service(move |stream: DoNotDisturbRequestStream| {
                            seeder.seed(stream);
                        });
                }
                Interface::FactoryReset => {
                    let seeder = seeder.clone();
                    let _ =
                        service_dir.add_fidl_service(move |stream: FactoryResetRequestStream| {
                            seeder.seed(stream);
                        });
                }
                Interface::Input => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: InputRequestStream| {
                        seeder.seed(stream);
                    });
                }
                Interface::Intl => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: IntlRequestStream| {
                        seeder.seed(stream);
                    });
                }
                Interface::Keyboard => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: KeyboardRequestStream| {
                        seeder.seed(stream);
                    });
                }
                Interface::Light => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: LightRequestStream| {
                        seeder.seed(stream);
                    });
                }
                Interface::NightMode => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: NightModeRequestStream| {
                        seeder.seed(stream);
                    });
                }
                Interface::Privacy => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: PrivacyRequestStream| {
                        seeder.seed(stream);
                    });
                }
                Interface::Setup => {
                    let seeder = seeder.clone();
                    let _ = service_dir.add_fidl_service(move |stream: SetupRequestStream| {
                        seeder.seed(stream);
                    });
                }
            }
        })
    }

    /// Derives a [Registrant] from this [Interface]. This is used convert a list of Interfaces
    /// specified in a configuration into actionable Registrants that can be used in the setting
    /// service.
    pub(crate) fn registrant(self) -> Registrant {
        let mut builder = registration::Builder::new(Registrar::Fidl(self.registration_fn()));
        let dependencies: Vec<Dependency> = self.dependencies();
        for dependency in dependencies {
            builder = builder.add_dependency(dependency);
        }

        builder.build()
    }
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_settings::PrivacyMarker;
    use fuchsia_async as fasync;
    use fuchsia_component::server::ServiceFs;
    use futures::StreamExt;

    use assert_matches::assert_matches;

    use crate::base::{Dependency, Entity, SettingType};
    use crate::handler::base::{Payload, Request};
    use crate::ingress::registration::Registrant;
    use crate::job::manager::Manager;
    use crate::job::source::Seeder;
    use crate::message::base::MessengerType;
    use crate::message::MessageHubUtil;
    use crate::service;

    use super::Interface;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_fidl_seeder_bringup() {
        let mut fs = ServiceFs::new();
        let delegate = service::MessageHub::create_hub();
        let job_manager_signature = Manager::spawn(&delegate).await;
        let job_seeder = Seeder::new(&delegate, job_manager_signature).await;

        // Using privacy since it uses a seeder for its fidl registration.
        let setting_type = SettingType::Privacy;

        let registrant: Registrant = Interface::Privacy.registrant();

        // Verify dependencies properly derived from the interface.
        assert!(registrant
            .get_dependencies()
            .contains(&Dependency::Entity(Entity::Handler(setting_type))));

        // Create handler to intercept messages.
        let mut rx = delegate
            .create(MessengerType::Addressable(service::Address::Handler(setting_type)))
            .await
            .expect("messenger should be created")
            .1;

        // Register and consume Registrant.
        registrant.register(&job_seeder, &mut fs.root_dir());

        // Spawn nested environment.
        let connector = fs.create_protocol_connector().expect("should create connector");
        fasync::Task::spawn(fs.collect()).detach();

        // Connect to the Privacy interface and request watching.
        let privacy_proxy =
            connector.connect_to_protocol::<PrivacyMarker>().expect("should connect to protocol");
        fasync::Task::spawn(async move {
            let _ = privacy_proxy.watch().await;
        })
        .detach();

        // Ensure handler receives request.
        assert_matches!(rx.next_of::<Payload>().await, Ok((Payload::Request(Request::Listen), _)));
    }
}
