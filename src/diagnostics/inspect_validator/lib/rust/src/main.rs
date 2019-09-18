// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Rust Puppet, receiving commands to drive the Rust Inspect library.
///
/// This code doesn't check for illegal commands such as deleting a node
/// that doesn't exist. Illegal commands should be (and can be) filtered
/// within the Validator program by running the command sequence against the
/// local ("data::Data") implementation before sending them to the puppets.
use {
    failure::{bail, format_err, Error, ResultExt},
    fidl_test_inspect_validate::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::*,
    fuchsia_syslog as syslog,
    futures::prelude::*,
    log::*,
    std::collections::HashMap,
};

enum Property {
    String(StringProperty),
    Int(IntProperty),
}

struct Actor {
    inspector: Inspector,
    nodes: HashMap<u32, Node>,
    properties: HashMap<u32, Property>,
}

impl Actor {
    fn act(&mut self, action: Action) -> Result<(), Error> {
        match action {
            Action::CreateNode(CreateNode { parent, id, name }) => {
                self.nodes.insert(id, self.find_parent(parent)?.create_child(name));
            }
            Action::DeleteNode(DeleteNode { id }) => {
                self.nodes.remove(&id);
            }
            Action::CreateIntProperty(CreateIntProperty { parent, id, name, value }) => {
                self.properties
                    .insert(id, Property::Int(self.find_parent(parent)?.create_int(name, value)));
            }
            Action::CreateStringProperty(CreateStringProperty { parent, id, name, value }) => {
                self.properties.insert(
                    id,
                    Property::String(self.find_parent(parent)?.create_string(name, value)),
                );
            }
            Action::DeleteProperty(DeleteProperty { id }) => {
                self.properties.remove(&id);
            }
            unexpected => {
                bail!("Unexpected action {:?}", unexpected);
            }
        };
        Ok(())
    }

    fn find_parent<'a>(&'a self, id: u32) -> Result<&'a Node, Error> {
        if id == 0 {
            Ok(self.inspector.root())
        } else {
            self.nodes.get(&id).ok_or_else(|| format_err!("Node {} not found", id))
        }
    }
}

async fn run_driver_service(mut stream: ValidateRequestStream) -> Result<(), Error> {
    let mut actor_maybe: Option<Actor> = None;
    while let Some(event) = stream.try_next().await? {
        match event {
            ValidateRequest::Initialize { params, responder } => {
                let inspector = match params.vmo_size {
                    Some(size) => Inspector::new_with_size(size as usize),
                    None => Inspector::new(),
                };
                responder
                    .send(inspector.vmo_handle_for_test(), TestResult::Ok)
                    .context("responding to initialize")?;
                actor_maybe = Some(Actor {
                    inspector,
                    nodes: HashMap::<u32, Node>::new(),
                    properties: HashMap::<u32, Property>::new(),
                });
            }
            ValidateRequest::Act { action, responder } => {
                let result = if let Some(a) = &mut actor_maybe {
                    match a.act(action) {
                        Ok(()) => TestResult::Ok,
                        Err(error) => {
                            warn!("Act saw illegal condition {:?}", error);
                            TestResult::Illegal
                        }
                    }
                } else {
                    TestResult::Illegal
                };
                responder.send(result)?;
            }
        }
    }
    Ok(())
}

enum IncomingService {
    Validate(ValidateRequestStream),
    // ... more services here
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&[]).expect("should not fail");
    info!("Puppet starting");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Validate);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 1;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Validate(stream)| {
        run_driver_service(stream).unwrap_or_else(|e| error!("ERROR in puppet's main: {:?}", e))
    });

    fut.await;
    Ok(())
}
