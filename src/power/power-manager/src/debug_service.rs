// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::message::Message;
use crate::node::Node;
use fidl_fuchsia_power_manager_debug as fdebug;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFsDir, ServiceObjLocal};
use futures::{TryFutureExt, TryStreamExt};
use log::*;
use std::collections::HashMap;
use std::rc::Rc;

/// Publishes a service to expose debug control of the Power Manager.
///
/// To enable this service, add `--args=enable_power_manager_debug=true` to fx set.
pub fn publish_debug_service<'a, 'b>(
    mut outgoing_svc_dir: ServiceFsDir<'a, ServiceObjLocal<'b, ()>>,
    nodes: HashMap<String, Rc<dyn Node>>,
) {
    info!("Starting debug service");

    outgoing_svc_dir.add_fidl_service(move |mut stream: fdebug::DebugRequestStream| {
        let nodes = nodes.clone();
        fasync::Task::local(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        fdebug::DebugRequest::Message { node_name, command, args, responder } => {
                            let mut result = match nodes.get(&node_name) {
                                Some(node) => {
                                    match node.handle_message(&Message::Debug(command, args)).await
                                    {
                                        Ok(_) => Ok(()),
                                        Err(PowerManagerError::Unsupported) => {
                                            Err(fdebug::MessageError::UnsupportedCommand)
                                        }
                                        Err(PowerManagerError::InvalidArgument(e)) => {
                                            error!("Invalid arguments: {:?}", e);
                                            Err(fdebug::MessageError::InvalidCommandArgs)
                                        }
                                        Err(e) => {
                                            error!("Unknown error occurred: {:?}", e);
                                            Err(fdebug::MessageError::Generic)
                                        }
                                    }
                                }
                                None => Err(fdebug::MessageError::InvalidNodeName),
                            };

                            responder.send(&mut result)?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("{:?}", e)),
        )
        .detach();
    });
}
