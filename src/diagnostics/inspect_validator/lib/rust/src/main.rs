// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Rust Puppet, receiving commands to drive the Rust Inspect library.
///
/// This code doesn't check for illegal commands such as deleting a node
/// that doesn't exist. Illegal commands should be (and can be) filtered
/// within the Validator program by running the command sequence against the
/// local ("data::Data") implementation before sending them to the puppets.
use fuchsia_inspect::Property as UsablePropertyTrait;
use {
    anyhow::{format_err, Context as _, Error},
    fidl_test_inspect_validate::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::*,
    fuchsia_syslog as syslog,
    fuchsia_zircon::HandleBased,
    futures::prelude::*,
    log::*,
    std::collections::HashMap,
};

#[derive(Debug)]
enum Property {
    // The names StringProperty, IntLinearHistogramProperty, etc. are built by macros such as
    // create_linear_histogram_property_fn! in src/lib/inspect/rust/fuchsia-inspect/src/lib.rs.
    // You won't find them by searching the codebase; instead search that file for macro_rules!
    // and go from there.
    // They're documented in https://fuchsia-docs.firebaseapp.com/rust/fuchsia_inspect/index.html.
    String(StringProperty),
    Int(IntProperty),
    Uint(UintProperty),
    Double(DoubleProperty),
    Bytes(BytesProperty),
    IntArray(IntArrayProperty),
    UintArray(UintArrayProperty),
    DoubleArray(DoubleArrayProperty),
    IntLinearHistogram(IntLinearHistogramProperty),
    UintLinearHistogram(UintLinearHistogramProperty),
    DoubleLinearHistogram(DoubleLinearHistogramProperty),
    IntExponentialHistogram(IntExponentialHistogramProperty),
    UintExponentialHistogram(UintExponentialHistogramProperty),
    DoubleExponentialHistogram(DoubleExponentialHistogramProperty),
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
            Action::CreateNumericProperty(CreateNumericProperty { parent, id, name, value }) => {
                self.properties.insert(
                    id,
                    match value {
                        Number::IntT(n) => {
                            Property::Int(self.find_parent(parent)?.create_int(name, n))
                        }
                        Number::UintT(n) => {
                            Property::Uint(self.find_parent(parent)?.create_uint(name, n))
                        }
                        Number::DoubleT(n) => {
                            Property::Double(self.find_parent(parent)?.create_double(name, n))
                        }
                        unknown => return Err(format_err!("Unknown number type {:?}", unknown)),
                    },
                );
            }
            Action::CreateBytesProperty(CreateBytesProperty { parent, id, name, value }) => {
                self.properties.insert(
                    id,
                    Property::Bytes(self.find_parent(parent)?.create_bytes(name, value)),
                );
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
            Action::SetNumber(SetNumber { id, value }) => {
                match (self.find_property(id)?, value) {
                    (Property::Int(p), Number::IntT(v)) => p.set(v),
                    (Property::Uint(p), Number::UintT(v)) => p.set(v),
                    (Property::Double(p), Number::DoubleT(v)) => p.set(v),
                    unexpected => {
                        return Err(format_err!("Illegal types {:?} for SetNumber", unexpected))
                    }
                };
            }
            Action::AddNumber(AddNumber { id, value }) => {
                match (self.find_property(id)?, value) {
                    (Property::Int(p), Number::IntT(v)) => p.add(v),
                    (Property::Uint(p), Number::UintT(v)) => p.add(v),
                    (Property::Double(p), Number::DoubleT(v)) => p.add(v),
                    unexpected => {
                        return Err(format_err!("Illegal types {:?} for AddNumber", unexpected))
                    }
                };
            }
            Action::SubtractNumber(SubtractNumber { id, value }) => {
                match (self.find_property(id)?, value) {
                    (Property::Int(p), Number::IntT(v)) => p.subtract(v),
                    (Property::Uint(p), Number::UintT(v)) => p.subtract(v),
                    (Property::Double(p), Number::DoubleT(v)) => p.subtract(v),
                    unexpected => {
                        return Err(format_err!(
                            "Illegal types {:?} for SubtractNumber",
                            unexpected
                        ))
                    }
                };
            }
            Action::SetString(SetString { id, value }) => match self.find_property(id)? {
                Property::String(p) => p.set(&value),
                unexpected => {
                    return Err(format_err!("Illegal property {:?} for SetString", unexpected))
                }
            },
            Action::SetBytes(SetBytes { id, value }) => match self.find_property(id)? {
                Property::Bytes(p) => p.set(&value),
                unexpected => {
                    return Err(format_err!("Illegal property {:?} for SetBytes", unexpected))
                }
            },
            Action::CreateArrayProperty(CreateArrayProperty {
                parent,
                id,
                name,
                slots,
                number_type,
            }) => {
                self.properties.insert(
                    id,
                    match number_type {
                        NumberType::Int => Property::IntArray(
                            self.find_parent(parent)?.create_int_array(name, slots as usize),
                        ),
                        NumberType::Uint => Property::UintArray(
                            self.find_parent(parent)?.create_uint_array(name, slots as usize),
                        ),
                        NumberType::Double => Property::DoubleArray(
                            self.find_parent(parent)?.create_double_array(name, slots as usize),
                        ),
                    },
                );
            }
            Action::ArraySet(ArraySet { id, index, value }) => {
                match (self.find_property(id)?, value) {
                    (Property::IntArray(p), Number::IntT(v)) => p.set(index as usize, v),
                    (Property::UintArray(p), Number::UintT(v)) => p.set(index as usize, v),
                    (Property::DoubleArray(p), Number::DoubleT(v)) => p.set(index as usize, v),
                    unexpected => {
                        return Err(format_err!("Illegal types {:?} for ArraySet", unexpected))
                    }
                };
            }
            Action::ArrayAdd(ArrayAdd { id, index, value }) => {
                match (self.find_property(id)?, value) {
                    (Property::IntArray(p), Number::IntT(v)) => p.add(index as usize, v),
                    (Property::UintArray(p), Number::UintT(v)) => p.add(index as usize, v),
                    (Property::DoubleArray(p), Number::DoubleT(v)) => p.add(index as usize, v),
                    unexpected => {
                        return Err(format_err!("Illegal types {:?} for ArrayAdd", unexpected))
                    }
                };
            }
            Action::ArraySubtract(ArraySubtract { id, index, value }) => {
                match (self.find_property(id)?, value) {
                    (Property::IntArray(p), Number::IntT(v)) => p.subtract(index as usize, v),
                    (Property::UintArray(p), Number::UintT(v)) => p.subtract(index as usize, v),
                    (Property::DoubleArray(p), Number::DoubleT(v)) => p.subtract(index as usize, v),
                    unexpected => {
                        return Err(format_err!("Illegal types {:?} for ArraySubtract", unexpected))
                    }
                };
            }
            Action::CreateLinearHistogram(CreateLinearHistogram {
                parent,
                id,
                name,
                floor,
                step_size,
                buckets,
            }) => {
                let buckets = buckets as usize;
                self.properties.insert(
                    id,
                    match (floor, step_size) {
                        (Number::IntT(floor), Number::IntT(step_size)) => {
                            Property::IntLinearHistogram(
                                self.find_parent(parent)?.create_int_linear_histogram(
                                    name,
                                    LinearHistogramParams { floor, step_size, buckets },
                                ),
                            )
                        }
                        (Number::UintT(floor), Number::UintT(step_size)) => {
                            Property::UintLinearHistogram(
                                self.find_parent(parent)?.create_uint_linear_histogram(
                                    name,
                                    LinearHistogramParams { floor, step_size, buckets },
                                ),
                            )
                        }
                        (Number::DoubleT(floor), Number::DoubleT(step_size)) => {
                            Property::DoubleLinearHistogram(
                                self.find_parent(parent)?.create_double_linear_histogram(
                                    name,
                                    LinearHistogramParams { floor, step_size, buckets },
                                ),
                            )
                        }
                        unexpected => {
                            return Err(format_err!(
                                "Illegal types {:?} for CreateLinearHistogram",
                                unexpected
                            ))
                        }
                    },
                );
            }
            Action::CreateExponentialHistogram(CreateExponentialHistogram {
                parent,
                id,
                name,
                floor,
                initial_step,
                step_multiplier,
                buckets,
            }) => {
                let buckets = buckets as usize;
                self.properties.insert(
                    id,
                    match (floor, initial_step, step_multiplier) {
                        (
                            Number::IntT(floor),
                            Number::IntT(initial_step),
                            Number::IntT(step_multiplier),
                        ) => Property::IntExponentialHistogram(
                            self.find_parent(parent)?.create_int_exponential_histogram(
                                name,
                                ExponentialHistogramParams {
                                    floor,
                                    initial_step,
                                    step_multiplier,
                                    buckets,
                                },
                            ),
                        ),
                        (
                            Number::UintT(floor),
                            Number::UintT(initial_step),
                            Number::UintT(step_multiplier),
                        ) => Property::UintExponentialHistogram(
                            self.find_parent(parent)?.create_uint_exponential_histogram(
                                name,
                                ExponentialHistogramParams {
                                    floor,
                                    initial_step,
                                    step_multiplier,
                                    buckets,
                                },
                            ),
                        ),
                        (
                            Number::DoubleT(floor),
                            Number::DoubleT(initial_step),
                            Number::DoubleT(step_multiplier),
                        ) => Property::DoubleExponentialHistogram(
                            self.find_parent(parent)?.create_double_exponential_histogram(
                                name,
                                ExponentialHistogramParams {
                                    floor,
                                    initial_step,
                                    step_multiplier,
                                    buckets,
                                },
                            ),
                        ),
                        unexpected => {
                            return Err(format_err!(
                                "Illegal types {:?} for CreateExponentialHistogram",
                                unexpected
                            ))
                        }
                    },
                );
            }
            Action::Insert(Insert { id, value }) => {
                match (self.find_property(id)?, value) {
                    (Property::IntLinearHistogram(p), Number::IntT(v)) => p.insert(v),
                    (Property::UintLinearHistogram(p), Number::UintT(v)) => p.insert(v),
                    (Property::DoubleLinearHistogram(p), Number::DoubleT(v)) => p.insert(v),
                    (Property::IntExponentialHistogram(p), Number::IntT(v)) => p.insert(v),
                    (Property::UintExponentialHistogram(p), Number::UintT(v)) => p.insert(v),
                    (Property::DoubleExponentialHistogram(p), Number::DoubleT(v)) => p.insert(v),
                    unexpected => {
                        return Err(format_err!("Illegal types {:?} for Insert", unexpected))
                    }
                };
            }
            Action::InsertMultiple(InsertMultiple { id, value, count }) => {
                match (self.find_property(id)?, value) {
                    (Property::IntLinearHistogram(p), Number::IntT(v)) => {
                        p.insert_multiple(v, count as usize)
                    }
                    (Property::UintLinearHistogram(p), Number::UintT(v)) => {
                        p.insert_multiple(v, count as usize)
                    }
                    (Property::DoubleLinearHistogram(p), Number::DoubleT(v)) => {
                        p.insert_multiple(v, count as usize)
                    }
                    (Property::IntExponentialHistogram(p), Number::IntT(v)) => {
                        p.insert_multiple(v, count as usize)
                    }
                    (Property::UintExponentialHistogram(p), Number::UintT(v)) => {
                        p.insert_multiple(v, count as usize)
                    }
                    (Property::DoubleExponentialHistogram(p), Number::DoubleT(v)) => {
                        p.insert_multiple(v, count as usize)
                    }
                    unexpected => {
                        return Err(format_err!(
                            "Illegal types {:?} for InsertMultiple",
                            unexpected
                        ))
                    }
                };
            }
            unexpected => {
                // "Illegal" is the appropriate response here, not "Unimplemented".
                // Known-Unimplemented actions should be matched explicitly.
                return Err(format_err!("Unexpected action {:?}", unexpected));
            }
        };
        Ok(())
    }

    fn find_parent<'a>(&'a self, id: u32) -> Result<&'a Node, Error> {
        if id == ROOT_ID {
            Ok(self.inspector.root())
        } else {
            self.nodes.get(&id).ok_or_else(|| format_err!("Node {} not found", id))
        }
    }

    fn find_property<'a>(&'a self, id: u32) -> Result<&'a Property, Error> {
        self.properties.get(&id).ok_or_else(|| format_err!("Property {} not found", id))
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
                    .send(inspector.duplicate_vmo().map(|v| v.into_handle()), TestResult::Ok)
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
