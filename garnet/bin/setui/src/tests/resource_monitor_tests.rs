// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::{BlueprintHandle, Context as AgentContext, Payload};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::monitor;
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

/// `TestMonitorAgent` exposes the monitor messenger delegate it receives at
/// creation.
#[derive(Debug)]
struct TestMonitorAgent;

impl TestMonitorAgent {
    fn create(callback: CallbackSender) -> BlueprintHandle {
        Arc::new(scaffold::agent::Blueprint::new(scaffold::agent::Generate::Async(Arc::new(
            move |mut context: AgentContext| -> BoxFuture<'static, ()> {
                callback
                    .unbounded_send(
                        context.resource_monitor_actor.clone().expect("should be present"),
                    )
                    .unwrap();

                Box::pin(async move {
                    // Immediately respond to all invocations
                    fasync::Task::spawn(async move {
                        while let Ok((.., client)) = context.receptor.next_of::<Payload>().await {
                            client.reply(Payload::Complete(Ok(())).into()).send().ack();
                        }
                    })
                    .detach();
                })
            },
        ))))
    }
}

// Ensures creating environment properly passes the correct facilities to
// monitors and agents.
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
                monitor_context_tx.unbounded_send(context).unwrap();
                Ok(())
            })
        });

    // Ensure the environment is brought up properly.
    assert!(EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
        .agents(&[TestMonitorAgent::create(monitor_actor_tx)])
        .resource_monitors(&[generate_monitor])
        .spawn_nested(ENV_NAME)
        .await
        .is_ok());

    // Use captured actor to start monitors.
    let monitor_messenger = monitor_actor_rx
        .next()
        .await
        .expect("should receive actor")
        .start_monitoring()
        .await
        .expect("should receive messenger");

    let mut monitor_context = monitor_context_rx.next().await.expect("should receive context");

    // Send Monitor command to monitor.
    monitor_messenger.message(monitor::Payload::Monitor.into()).send().ack();

    // Ensure command is received by the monitor.
    assert!(matches!(
        monitor_context
            .receptor
            .next_of::<monitor::Payload>()
            .await
            .expect("payload should be present")
            .0,
        monitor::Payload::Monitor
    ));
}
