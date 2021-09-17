// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{any::TypeId, rc::Rc};

use crate::{
    animation::{KeyedObject, KeyedProperty, LinearAnimation},
    artboard::Artboard,
    backboard::Backboard,
    component::Component,
    core::{self, BinaryReader, Core, Object},
    importers::{
        ArtboardImporter, ImportStack, ImportStackObject, KeyedObjectImporter,
        KeyedPropertyImporter, LinearAnimationImporter,
    },
    runtime_header::RuntimeHeader,
    status_code::StatusCode,
};

/// Major version number supported by the runtime.
const MAJOR_VERSION: u32 = 7;
/// Minor version number supported by the runtime.
const _MINOR_VERSION: u32 = 0;

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
) -> Option<(Object, TypeId)> {
    let id = core::get_type_id(reader.read_var_u64()? as u16)?;
    let (core, object) = <dyn Core>::from_type_id(id)?;
    objects.push(Rc::clone(&core));

    loop {
        let property_key = reader.read_var_u64()? as u16;
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

    Some((object, id))
}

#[derive(Debug)]
pub struct File {
    backboard: Object<Backboard>,
    artboards: Vec<Object<Artboard>>,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    objects: Vec<Rc<dyn Core>>,
}

impl File {
    pub fn import(reader: &mut BinaryReader<'_>) -> Result<Self, ImportError> {
        let header = RuntimeHeader::read(reader).ok_or(ImportError::Malformed)?;

        if header.major_version() != MAJOR_VERSION {
            return Err(ImportError::UnsupportedVersion);
        }

        Self::read(reader, &header).ok_or(ImportError::Malformed)
    }

    fn read(reader: &mut BinaryReader<'_>, header: &RuntimeHeader) -> Option<Self> {
        let mut objects = Vec::new();
        let mut import_stack = ImportStack::default();
        let mut backboard_option = None;
        let mut artboards = Vec::new();

        while !reader.reached_end() {
            let (object, id) = read_runtime_object(reader, header, &mut objects)?;

            let stack_object: Option<Box<dyn ImportStackObject>> =
                if let Some(artboard) = object.try_cast::<Artboard>() {
                    Some(Box::new(ArtboardImporter::new(artboard)))
                } else if let Some(animation) = object.try_cast::<LinearAnimation>() {
                    Some(Box::new(LinearAnimationImporter::new(animation)))
                } else if let Some(keyed_object) = object.try_cast::<KeyedObject>() {
                    Some(Box::new(KeyedObjectImporter::new(keyed_object)))
                } else if let Some(keyed_property) = object.try_cast::<KeyedProperty>() {
                    let importer = import_stack
                        .latest::<LinearAnimationImporter>(TypeId::of::<LinearAnimation>())?;
                    Some(Box::new(KeyedPropertyImporter::new(importer.animation(), keyed_property)))
                } else {
                    None
                };

            if import_stack.make_latest(id, stack_object) != StatusCode::Ok {
                return None;
            }

            if object.as_ref().import(object.clone(), &import_stack) == StatusCode::Ok {
                if let Some(backboard) = object.try_cast::<Backboard>() {
                    backboard_option = Some(backboard);
                }

                if let Some(artboard) = object.try_cast::<Artboard>() {
                    artboards.push(artboard);
                }
            }
        }

        if import_stack.resolve() != StatusCode::Ok {
            return None;
        }

        Some(Self { backboard: backboard_option?, artboards, objects })
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

    pub fn artboards(&self) -> impl Iterator<Item = &Object<Artboard>> {
        self.artboards.iter()
    }
}
