// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_stresstest::{
        Action as FidlAction, ActionIteratorMarker, ActionIteratorRequest, ActorRequest,
        ActorRequestStream, Error,
    },
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon_sys::ZX_CHANNEL_MAX_MSG_BYTES,
    futures::{future::BoxFuture, StreamExt, TryStreamExt},
    rand::{rngs::SmallRng, SeedableRng},
    rust_measure_tape_for_action::Measurable,
};

enum OutgoingProtocols {
    Actor(ActorRequestStream),
}

/// This structure represents a single action that can be run by an actor.
/// These actions are exposed to the stress test runner over FIDL using the Actor protocol.
pub struct Action<D> {
    /// The name of this action, as it will appear to the stress test runner
    pub name: &'static str,

    /// The function that will be invoked when this action is asked to execute.
    /// This function returns a boxed future that will be awaited on to allow
    /// async work to be done.
    pub run: for<'a> fn(&'a mut D, SmallRng) -> BoxFuture<'a, Result<()>>,
}

impl<D> Action<D> {
    /// Converts the rust object into its FIDL equivalent to be sent across the wire.
    fn to_fidl(&self) -> FidlAction {
        FidlAction { name: Some(self.name.to_string()), ..FidlAction::EMPTY }
    }
}

/// This is an indefinite loop that is run inside a actor component. Clients use the Actor
/// protocol to run test actions.
///
/// This method will serve the Actor protocol over its outgoing directory and wait for
/// exactly one client to connect.
///
/// NOTE: This method takes and serves the process' outgoing directory handle.
/// This handle should not be taken before this method is invoked.
///
/// NOTE: The actor library expects exactly one connection to the Actor protocol in this method.
pub async fn actor_loop<D>(mut data: D, actions: Vec<Action<D>>) -> Result<()> {
    let mut service_fs = ServiceFs::new();
    service_fs.dir("svc").add_fidl_service(OutgoingProtocols::Actor);
    service_fs.take_and_serve_directory_handle()?;

    // Wait for a client to connect
    let OutgoingProtocols::Actor(mut stream) = service_fs
        .next()
        .await
        .ok_or(format_err!("Could not get next connection to Actor protocol"))?;

    while let Some(request) = stream
        .try_next()
        .await
        .map_err(|e| format_err!("FIDL error in call to Actor protocol: {}", e))?
    {
        match request {
            ActorRequest::GetActions { responder } => {
                let (client_end, mut stream) = create_request_stream::<ActionIteratorMarker>()?;
                responder.send(client_end)?;

                let mut iter = actions.iter().map(|c| c.to_fidl());

                while let Some(ActionIteratorRequest::GetNext { responder }) =
                    stream.try_next().await?
                {
                    let mut bytes_used: usize = 32;
                    let mut action_count = 0;

                    // Determine how many actions can be sent in a single FIDL message,
                    // accounting for the header and `Action` size.
                    for action in iter.clone() {
                        bytes_used += action.measure().num_bytes;
                        if bytes_used > ZX_CHANNEL_MAX_MSG_BYTES as usize {
                            break;
                        }
                        action_count += 1;
                    }
                    responder.send(&mut iter.by_ref().take(action_count))?;

                    // There are no more actions left to return. The client has gotten
                    // an empty response, so they also know that there are no more actions.
                    // Close this channel.
                    if action_count == 0 {
                        break;
                    }
                }
            }
            ActorRequest::Run { action_name, seed, responder } => {
                if let Some(action) = actions.iter().find(|action| action.name == action_name) {
                    let rng = SmallRng::seed_from_u64(seed);
                    if let Err(e) = (action.run)(&mut data, rng).await {
                        // The action failed. Return the error chain as an unstructured
                        // error message.
                        let chain: Vec<String> = e.chain().map(|c| c.to_string()).collect();
                        let chain = chain.join(" -> ");
                        responder.send(Some(&mut Error::ErrorString(chain)))?;
                    } else {
                        // The action succeeded
                        responder.send(None)?;
                    }
                } else {
                    responder
                        .send(Some(&mut Error::ErrorString("Invalid action name".to_string())))?;
                }
            }
        }
    }

    Ok(())
}
