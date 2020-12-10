// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{BlueprintHandle, Context as AgentContext};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::internal::agent;
use crate::internal::monitor;
use crate::monitor::base::monitor::Context as MonitorContext;
use crate::monitor::environment::Actor;
use crate::tests::scaffold;
use crate::EnvironmentBuilder;
use anyhow::Error;
use fuchsia_async as fasync;
use futures::channel::mpsc::UnboundedSender;
use futures::future::BoxFuture;
use futures::StreamExt;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_resource_monitor_test_environment";

type CallbackSender = UnboundedSender<Actor>;

/// `TestMonitorAgent` exposes the monitor messenger factory it receives at
/// creation.
#[derive(Debug)]
struct TestMonitorAgent;

impl TestMonitorAgent {
    pub fn create(callback: CallbackSender) -> BlueprintHandle {
        Arc::new(scaffold::agent::Blueprint::new(
            scaffold::agent::Generate::Async(Arc::new(
                move |mut context: AgentContext| -> BoxFuture<'static, ()> {
                    callback
                        .unbounded_send(
                            context.resource_monitor_actor.clone().expect("should be present"),
                        )
                        .ok();

                    Box::pin(async move {
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
    // Create a channel to receive the agent's monitor actor.
    let (monitor_actor_tx, mut monitor_actor_rx) = futures::channel::mpsc::unbounded::<Actor>();

    // Create a channel to receive the monitor context.
    let (monitor_context_tx, mut monitor_context_rx) =
        futures::channel::mpsc::unbounded::<MonitorContext>();

    // Create a monitor that exposes the MonitorContext received.
    let generate_monitor =
        Arc::new(move |context: MonitorContext| -> BoxFuture<'_, Result<(), Error>> {
            let monitor_context_tx = monitor_context_tx.clone();

            Box::pin(async move {
                monitor_context_tx.unbounded_send(context).ok();
                Ok(())
            })
        });

    // Ensure the environment is brought up properly.
    assert!(EnvironmentBuilder::new(InMemoryStorageFactory::create())
        .agents(&[TestMonitorAgent::create(monitor_actor_tx)])
        .resource_monitors(&[generate_monitor])
        .settings(&[])
        .spawn_nested(ENV_NAME)
        .await
        .is_ok());

    // Use captured actor to generate environment.
    let mut receptor = monitor_actor_rx
        .next()
        .await
        .expect("should receive actor")
        .start_monitoring()
        .await
        .expect("should receive receptor");

    // Use captured messenger factory to create a top level Messenger for the
    // agent.
    let monitor_context = monitor_context_rx.next().await.expect("should receive context");

    monitor_context.messenger.message(monitor::Payload::Monitor).send().ack();

    // Ensure command is received by the agent.
    assert!(matches!(
        receptor.next_payload().await.expect("payload should be present").0,
        monitor::Payload::Monitor
    ));
}
