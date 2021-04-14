// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::rc::Rc;

use crate::{
    animation::{Animation, KeyFrame, KeyedObject, KeyedProperty, LinearAnimation},
    artboard::Artboard,
    backboard::Backboard,
    component::Component,
    core::{self, BinaryReader, Core, Object},
    runtime_header::RuntimeHeader,
    status_code::StatusCode,
};

/// Major version number supported by the runtime.
const MAJOR_VERSION: u32 = 6;
/// Minor version number supported by the runtime.
const _MINOR_VERSION: u32 = 3;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ImportError {
    /// Indicates that the Rive file is not supported by this runtime.
    UnsupportedVersion,
    /// Indicates that the there is a formatting problem in the file itself.
    Malformed,
}

fn read_runtime_object(
    reader: &mut BinaryReader<'_>,
    header: &RuntimeHeader,
    objects: &mut Vec<Rc<dyn Core>>,
) -> Option<Object> {
    let id = core::get_type_id(reader.read_var_u64()?)?;
    let (core, object) = <dyn Core>::from_type_id(id)?;
    dbg!(&core);
    objects.push(Rc::clone(&core));

    loop {
        let property_key = reader.read_var_u64()?;
        if property_key == 0 {
            break;
        }

        if !core.write(property_key, reader) {
            // todo!("try to find core property keys");

            match header.property_file_id(property_key as u32)? {
                0 => {
                    reader.read_var_u64()?;
                }
                1 => {
                    reader.read_string()?;
                }
                2 => {
                    reader.read_f32()?;
                }
                3 => {
                    reader.read_u32()?;
                }
                _ => return None,
            }
        }
    }

    Some(object)
}

fn read_as<T: Core>(
    reader: &mut BinaryReader<'_>,
    header: &RuntimeHeader,
    objects: &mut Vec<Rc<dyn Core>>,
) -> Option<Object<T>> {
    read_runtime_object(reader, header, objects)?.try_cast()
}

#[derive(Debug)]
pub struct File {
    backboard: Object<Backboard>,
    artboards: Vec<Object<Artboard>>,
    objects: Vec<Rc<dyn Core>>,
}

impl File {
    pub fn import(reader: &mut BinaryReader<'_>) -> Result<Self, ImportError> {
        let header = RuntimeHeader::read(reader).ok_or(ImportError::Malformed)?;

        if header.major_version() > MAJOR_VERSION {
            return Err(ImportError::UnsupportedVersion);
        }

        Self::read(reader, &header).ok_or(ImportError::Malformed)
    }

    fn read(reader: &mut BinaryReader<'_>, header: &RuntimeHeader) -> Option<Self> {
        let mut objects = Vec::new();
        let backboard = read_as::<Backboard>(reader, &header, &mut objects)?;
        let mut artboards = Vec::new();

        for _ in 0..reader.read_var_u64()? {
            let num_objects = reader.read_var_u64()?;
            if num_objects == 0 {
                return None;
            }

            let artboard_object = read_as::<Artboard>(reader, header, &mut objects)?;
            let artboard = artboard_object.as_ref();
            artboard.push_object(artboard_object.clone().into());
            artboards.push(artboard_object.clone());

            for _ in 1..num_objects {
                // todo!("don't return here");
                artboard.push_object(read_runtime_object(reader, header, &mut objects)?);
            }

            for _ in 0..reader.read_var_u64()? {
                if let Some(animation) = read_as::<Animation>(reader, header, &mut objects) {
                    artboard.push_animation(animation.clone());

                    if let Some(linear_animation) = animation.try_cast::<LinearAnimation>() {
                        for _ in 0..reader.read_var_u64()? {
                            if let Some(keyed_object) =
                                read_as::<KeyedObject>(reader, header, &mut objects)
                            {
                                let linear_animation = linear_animation.as_ref();
                                linear_animation.push_keyed_object(keyed_object.clone());

                                for _ in 0..reader.read_var_u64()? {
                                    if let Some(keyed_property) =
                                        read_as::<KeyedProperty>(reader, header, &mut objects)
                                    {
                                        keyed_object
                                            .as_ref()
                                            .push_keyed_property(keyed_property.clone());

                                        for _ in 0..reader.read_var_u64()? {
                                            if let Some(key_frame) =
                                                read_as::<KeyFrame>(reader, header, &mut objects)
                                            {
                                                key_frame
                                                    .as_ref()
                                                    .compute_seconds(linear_animation.fps());
                                                keyed_property.as_ref().push_key_frame(key_frame);
                                            } else {
                                                continue;
                                            }
                                        }
                                    } else {
                                        continue;
                                    }
                                }
                            } else {
                                continue;
                            }
                        }
                    }
                } else {
                    continue;
                }
            }

            if artboard.initialize() != StatusCode::Ok {
                return None;
            }
        }

        Some(Self { backboard, artboards, objects })
    }

    pub fn backboard(&self) -> Object<Backboard> {
        self.backboard.clone()
    }

    pub fn artboard(&self) -> Option<Object<Artboard>> {
        self.artboards.get(0).cloned()
    }

    pub fn get_artboard(&self, name: &str) -> Option<Object<Artboard>> {
        self.artboards
            .iter()
            .find(|artboard| artboard.cast::<Component>().as_ref().name() == name)
            .cloned()
    }
}
