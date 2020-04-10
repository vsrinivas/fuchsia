// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(test)]
use {
    crate::agent::restore_agent::RestoreAgent,
    crate::registry::base::State,
    crate::registry::device_storage::testing::*,
    crate::registry::setting_handler::{
        controller, persist::controller as data_controller,
        persist::ClientProxy as DataClientProxy, persist::Handler as DataHandler, persist::Storage,
        ClientProxy, ControllerError, Handler,
    },
    crate::switchboard::base::{
        DoNotDisturbInfo, SettingRequest, SettingResponseResult, SettingType,
    },
    crate::{Environment, EnvironmentBuilder},
    anyhow::Error,
    async_trait::async_trait,
    futures::lock::Mutex,
    std::marker::PhantomData,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_setting_handler_test_environment";

/// The Control trait provides static functions that control the behavior of
/// test controllers. Since controllers are created from a trait themselves,
/// we must specify this functionality as a trait so that the impl types can
/// be supplied as generic parameters.
trait Control {
    fn should_init_succeed() -> bool;
}

/// SucceedControl provides a Control implementation that will succeed on
/// initialization.
struct SucceedControl {}

impl Control for SucceedControl {
    fn should_init_succeed() -> bool {
        true
    }
}

/// FailControl provides a Control implementation that will fail on
/// initialization.
struct FailControl {}

impl Control for FailControl {
    fn should_init_succeed() -> bool {
        false
    }
}

/// Controller is a simple controller test implementation that refers to a
/// Control type for how to behave.
struct Controller<C: Control + Sync + Send + 'static> {
    _data: PhantomData<C>,
}

#[async_trait]
impl<C: Control + Sync + Send + 'static> controller::Create for Controller<C> {
    async fn create(_: ClientProxy) -> Result<Self, ControllerError> {
        if C::should_init_succeed() {
            Ok(Self { _data: PhantomData })
        } else {
            Err(ControllerError::InitFailure { description: "failure".to_string() })
        }
    }
}

#[async_trait]
impl<C: Control + Sync + Send + 'static> controller::Handle for Controller<C> {
    async fn handle(&self, _: SettingRequest) -> Option<SettingResponseResult> {
        return None;
    }

    async fn change_state(&mut self, _: State) {}
}

/// The DataController is a controller implementation with storage that
/// defers to a Control type for how to behave.
struct DataController<C: Control + Sync + Send + 'static, S: Storage> {
    _control: PhantomData<C>,
    _storage: PhantomData<S>,
}

#[async_trait]
impl<C: Control + Sync + Send + 'static, S: Storage> data_controller::Create<S>
    for DataController<C, S>
{
    async fn create(_: DataClientProxy<S>) -> Result<Self, ControllerError> {
        if C::should_init_succeed() {
            Ok(Self { _control: PhantomData, _storage: PhantomData })
        } else {
            Err(ControllerError::InitFailure { description: "failure".to_string() })
        }
    }
}

#[async_trait]
impl<C: Control + Sync + Send + 'static, S: Storage> controller::Handle for DataController<C, S> {
    async fn handle(&self, _: SettingRequest) -> Option<SettingResponseResult> {
        return None;
    }

    async fn change_state(&mut self, _: State) {}
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_spawn() {
    // Exercises successful spawn of a simple controller.
    verify_handler::<SucceedControl>().await;
    // Exercises failed spawn of a simple controller.
    verify_handler::<FailControl>().await;
    // Exercises successful spawn of a data controller.
    verify_data_handler::<SucceedControl>().await;
    // Exercises failed spawn of a data controller.
    verify_data_handler::<FailControl>().await;
}

async fn verify_handler<C: Control + Sync + Send + 'static>() {
    verify_environment_startup(
        EnvironmentBuilder::new(InMemoryStorageFactory::create())
            .handler(SettingType::Unknown, Box::new(Handler::<Controller<C>>::spawn))
            .agents(&[Arc::new(Mutex::new(RestoreAgent::new()))])
            .settings(&[SettingType::Unknown])
            .spawn_nested(ENV_NAME)
            .await,
    )
    .await;
}

async fn verify_data_handler<C: Control + Sync + Send + 'static>() {
    verify_environment_startup(
        EnvironmentBuilder::new(InMemoryStorageFactory::create())
            .handler(
                SettingType::Unknown,
                Box::new(
                    DataHandler::<DoNotDisturbInfo, DataController<C, DoNotDisturbInfo>>::spawn,
                ),
            )
            .agents(&[Arc::new(Mutex::new(RestoreAgent::new()))])
            .settings(&[SettingType::Unknown])
            .spawn_nested(ENV_NAME)
            .await,
    )
    .await;
}

async fn verify_environment_startup(spawn_result: Result<Environment, Error>) {
    if let Ok(environment) = spawn_result {
        if let Ok(result) = environment.completion_rx.await {
            assert!(result.is_ok());
        } else {
            panic!("Completion rx should have returned the environment initialization result");
        }
    } else {
        panic!("Should have successfully created environment");
    }
}
