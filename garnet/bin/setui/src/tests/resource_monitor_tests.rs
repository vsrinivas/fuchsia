// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{BlueprintHandle, Context as AgentContext};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::internal::agent;
use crate::internal::monitor;
use crate::message::base::{Audience, MessengerType};
use crate::monitor::base::Context as MonitorContext;
use crate::tests::scaffold;
use crate::EnvironmentBuilder;
use anyhow::Error;
use fuchsia_async as fasync;
use futures::channel::mpsc::UnboundedSender;
use futures::future::BoxFuture;
use futures::StreamExt;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_resource_monitor_test_environment";

type CallbackSender = UnboundedSender<monitor::message::Factory>;

/// `TestMonitorAgent` exposes the monitor messenger factory it receives at
/// creation.
#[derive(Debug)]
struct TestMonitorAgent;

impl TestMonitorAgent {
    pub fn create(callback: CallbackSender) -> BlueprintHandle {
        Arc::new(scaffold::agent::Blueprint::new(
            scaffold::agent::Generate::Async(Arc::new(
                move |mut context: AgentContext| -> BoxFuture<'static, ()> {
                    let monitor_messenger_factory = monitor::message::create_hub();
                    callback.unbounded_send(monitor_messenger_factory.clone()).ok();

                    let monitor_context = MonitorContext {
                        monitor_messenger_factory: monitor_messenger_factory.clone(),
                    };

                    let actor = context.resource_monitor_actor.clone();

                    Box::pin(async move {
                        // Create monitors
                        assert!(actor
                            .expect("monitor should be present")
                            .start_monitoring(monitor_context)
                            .await
                            .is_ok());

                        // Immediately respond to all invocations
                        fasync::Task::spawn(async move {
                            while let Ok((.., client)) = context.receptor.next_payload().await {
                                client.reply(agent::Payload::Complete(Ok(()))).send().ack();
                            }
                        })
                        .detach();
                    })
                },
            )),
            "test_monitor_agent",
        ))
    }
}

/// Ensures creating environment properly passes the correct facilities to
/// monitors and agents.
#[fuchsia_async::run_until_stalled(test)]
async fn test_environment_bringup() {
    // Create a channel to receive the agent's monitor messenger factory.
    let (agent_monitor_messenger_factory_tx, mut agent_monitor_messenger_factory_rx) =
        futures::channel::mpsc::unbounded::<monitor::message::Factory>();

    // Create a channel to receive the monitor's messenger factory.
    let (monitor_messenger_factory_tx, mut monitor_messenger_factory_rx) =
        futures::channel::mpsc::unbounded::<monitor::message::Factory>();

    // Create a monitor that exposes the MonitorContext received.
    let generate_monitor =
        Arc::new(move |context: MonitorContext| -> BoxFuture<'_, Result<(), Error>> {
            let monitor_messenger_factory_tx = monitor_messenger_factory_tx.clone();

            Box::pin(async move {
                monitor_messenger_factory_tx.unbounded_send(context.monitor_messenger_factory).ok();
                Ok(())
            })
        });

    // Ensure the environment is brought up properly.
    assert!(EnvironmentBuilder::new(InMemoryStorageFactory::create())
        .agents(&[TestMonitorAgent::create(agent_monitor_messenger_factory_tx)])
        .resource_monitors(&[generate_monitor])
        .settings(&[])
        .spawn_nested(ENV_NAME)
        .await
        .is_ok());

    // Use captured messenger factory to create a top level Message receptor for
    // the monitor.
    let (.., mut monitor_receptor) = monitor_messenger_factory_rx
        .next()
        .await
        .expect("should receive factory")
        .create(MessengerType::Unbound)
        .await
        .expect("should be able to create messenger");

    // Use captured messenger factory to create a top level Messenger for the
    // agent.
    let (agent_messenger, ..) = agent_monitor_messenger_factory_rx
        .next()
        .await
        .expect("should receive factory")
        .create(MessengerType::Unbound)
        .await
        .expect("should be able to create messenger");

    // Send command from agent messenger.
    agent_messenger.message(monitor::Payload::Monitor, Audience::Broadcast).send().ack();

    // Ensure command is received by the monitor.
    assert!(matches!(
        monitor_receptor.next_payload().await.expect("payload should be present").0,
        monitor::Payload::Monitor
    ));
}
