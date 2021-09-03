// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{Dependency, Entity, SettingType};
use crate::handler::base::Error;
use crate::ingress::registration::{self, Registrant, Registrar};
use crate::job::source::Seeder;
use crate::service::message::Delegate;
use fidl_fuchsia_settings::{
    AccessibilityRequestStream, AudioRequestStream, DeviceRequestStream, DisplayRequestStream,
    DoNotDisturbRequestStream, FactoryResetRequestStream, InputRequestStream, IntlRequestStream,
    LightRequestStream, NightModeRequestStream, PrivacyRequestStream, SetupRequestStream,
};
use fuchsia_component::server::{ServiceFsDir, ServiceObj};
use fuchsia_zircon;

impl From<Error> for fuchsia_zircon::Status {
    fn from(error: Error) -> fuchsia_zircon::Status {
        match error {
            Error::UnhandledType(_) => fuchsia_zircon::Status::UNAVAILABLE,
            _ => fuchsia_zircon::Status::INTERNAL,
        }
    }
}

// TODO(fxbug.dev/76287): Remove this conversion. It is only in place while configurations still
// reference SettingTypes instead of interfaces to declare services. Configurations should define
// said interfaces as constants separate from the Interface enumeration declared in code.
impl From<&SettingType> for Interface {
    fn from(item: &SettingType) -> Self {
        match item {
            SettingType::Accessibility => Interface::Accessibility,
            SettingType::Audio => Interface::Audio,
            SettingType::Device => Interface::Device,
            SettingType::Display => Interface::Display(display::InterfaceFlags::BASE),
            SettingType::DoNotDisturb => Interface::DoNotDisturb,
            SettingType::FactoryReset => Interface::FactoryReset,
            SettingType::Input => Interface::Input,
            SettingType::Intl => Interface::Intl,
            SettingType::Light => Interface::Light,
            SettingType::LightSensor => Interface::Display(display::InterfaceFlags::LIGHT_SENSOR),
            SettingType::NightMode => Interface::NightMode,
            SettingType::Privacy => Interface::Privacy,
            SettingType::Setup => Interface::Setup,
            // Support future expansion of FIDL.
            #[allow(unreachable_patterns)]
            _ => {
                panic!("unsupported SettingType for Interface conversion: {:?}", item);
            }
        }
    }
}

/// [Interface] defines the FIDL interfaces supported by the settings service.
#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub enum Interface {
    Audio,
    Accessibility,
    Device,
    Display(display::InterfaceFlags),
    DoNotDisturb,
    FactoryReset,
    Input,
    Intl,
    Light,
    NightMode,
    Privacy,
    Setup,
}

pub mod display {
    use bitflags::bitflags;

    bitflags! {
        /// The Display interface covers a number of feature spaces, each handled by a different
        /// entity dependency. The flags below allow the scope of these features to be specified by
        /// the interface.
        pub struct InterfaceFlags: u64 {
            const BASE = 1 << 0;
            const LIGHT_SENSOR = 1 << 1;
        }
    }
}

/// [Register] defines the closure implemented for interfaces to bring up support. Each interface
/// handler is given access to the MessageHub [Delegate] for communication within the service and
/// [ServiceFsDir] to register as the designated handler for the interface.
pub(crate) type Register = Box<
    dyn for<'a> FnOnce(&Delegate, &Seeder, &mut ServiceFsDir<'_, ServiceObj<'a, ()>>) + Send + Sync,
>;

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
            Interface::Device => {
                vec![Dependency::Entity(Entity::Handler(SettingType::Device))]
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
        Box::new(
            move |delegate: &Delegate,
                  seeder: &Seeder,
                  service_dir: &mut ServiceFsDir<'_, ServiceObj<'_, ()>>| {
                let delegate = delegate.clone();
                match self {
                    Interface::Audio => {
                        service_dir.add_fidl_service(move |stream: AudioRequestStream| {
                            crate::audio::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::Device => {
                        service_dir.add_fidl_service(move |stream: DeviceRequestStream| {
                            crate::device::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::Accessibility => {
                        service_dir.add_fidl_service(move |stream: AccessibilityRequestStream| {
                            crate::accessibility::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::Display(_) => {
                        service_dir.add_fidl_service(move |stream: DisplayRequestStream| {
                            crate::display::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::DoNotDisturb => {
                        service_dir.add_fidl_service(move |stream: DoNotDisturbRequestStream| {
                            crate::do_not_disturb::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::FactoryReset => {
                        let seeder = seeder.clone();
                        service_dir.add_fidl_service(move |stream: FactoryResetRequestStream| {
                            seeder.seed(stream);
                        });
                    }
                    Interface::Input => {
                        service_dir.add_fidl_service(move |stream: InputRequestStream| {
                            crate::input::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::Intl => {
                        service_dir.add_fidl_service(move |stream: IntlRequestStream| {
                            crate::intl::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::Light => {
                        service_dir.add_fidl_service(move |stream: LightRequestStream| {
                            crate::light::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::NightMode => {
                        service_dir.add_fidl_service(move |stream: NightModeRequestStream| {
                            crate::night_mode::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::Privacy => {
                        service_dir.add_fidl_service(move |stream: PrivacyRequestStream| {
                            crate::privacy::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                    Interface::Setup => {
                        service_dir.add_fidl_service(move |stream: SetupRequestStream| {
                            crate::setup::fidl_io::spawn(delegate.clone(), stream);
                        });
                    }
                }
            },
        )
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
    use super::Interface;
    use crate::base::{Dependency, Entity, SettingType};
    use crate::handler::base::{Payload, Request};
    use crate::ingress::registration::Registrant;
    use crate::job::source::Seeder;
    use crate::message::base::MessengerType;
    use crate::message::MessageHubUtil;
    use crate::service;
    use fidl_fuchsia_settings::PrivacyMarker;
    use fuchsia_async as fasync;
    use fuchsia_component::server::ServiceFs;
    use futures::StreamExt;
    use matches::assert_matches;

    const ENV_NAME: &str = "settings_service_fidl_environment";

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_fidl_bringup() {
        let mut fs = ServiceFs::new();
        let delegate = service::MessageHub::create_hub();
        let job_manager_signature = delegate
            .create(MessengerType::Unbound)
            .await
            .expect("messenger should be created")
            .0
            .get_signature();
        let job_seeder = Seeder::new(&delegate, job_manager_signature).await;

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
        registrant.register(&delegate, &job_seeder, &mut fs.root_dir());

        // Spawn nested environment.
        let nested_environment =
            fs.create_salted_nested_environment(ENV_NAME).expect("should create environment");
        fasync::Task::spawn(fs.collect()).detach();

        // Connect to the Privacy interface and request watching.
        let privacy_proxy = nested_environment
            .connect_to_protocol::<PrivacyMarker>()
            .expect("should connect to protocol");
        fasync::Task::spawn(async move {
            let _ = privacy_proxy.watch().await;
        })
        .detach();

        // Ensure handler receives request.
        assert_matches!(rx.next_of::<Payload>().await, Ok((Payload::Request(Request::Listen), _)));
    }
}
