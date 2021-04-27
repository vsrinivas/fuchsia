// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{any::TypeId, collections::HashMap, fmt};

use crate::{core::AsAny, status_code::StatusCode};

pub trait ImportStackObject: AsAny + fmt::Debug {
    fn resolve(&self) -> StatusCode {
        StatusCode::Ok
    }
}

#[derive(Debug, Default)]
pub struct ImportStack {
    latests: HashMap<TypeId, Box<dyn ImportStackObject>>,
}

impl ImportStack {
    pub fn latest<T: ImportStackObject>(&self, id: TypeId) -> Option<&T> {
        self.latests.get(&id).and_then(|object| (&**object).as_any().downcast_ref())
    }

    pub fn make_latest(
        &mut self,
        id: TypeId,
        object: Option<Box<dyn ImportStackObject>>,
    ) -> StatusCode {
        if let Some(removed) = self.latests.remove(&id) {
            let code = removed.resolve();
            if code != StatusCode::Ok {
                return code;
            }
        }

        if let Some(object) = object {
            self.latests.insert(id, object);
        }

        StatusCode::Ok
    }

    pub fn resolve(&self) -> StatusCode {
        self.latests
            .values()
            .map(|object| object.resolve())
            .find(|&code| code != StatusCode::Ok)
            .unwrap_or(StatusCode::Ok)
    }
}
