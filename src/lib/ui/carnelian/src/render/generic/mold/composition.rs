// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{collections::BTreeMap, convert::TryFrom};

use crate::{
    color::Color,
    render::generic::{mold::Mold, Composition, Layer},
};

#[derive(Clone, Debug)]
pub struct MoldComposition {
    pub(crate) layers: BTreeMap<u16, Layer<Mold>>,
    pub(crate) background_color: Color,
}

impl Composition<Mold> for MoldComposition {
    fn new(background_color: Color) -> Self {
        Self { layers: BTreeMap::new(), background_color }
    }

    fn with_layers(layers: impl IntoIterator<Item = Layer<Mold>>, background_color: Color) -> Self {
        Self {
            layers: layers
                .into_iter()
                .enumerate()
                .map(|(i, layer)| (u16::try_from(i).expect("too many layers"), layer))
                .collect(),
            background_color,
        }
    }

    fn clear(&mut self) {
        self.layers.clear();
    }

    fn insert(&mut self, order: u16, layer: Layer<Mold>) -> Option<Layer<Mold>> {
        self.layers.insert(order, layer)
    }

    fn remove(&mut self, order: u16) -> Option<Layer<Mold>> {
        self.layers.remove(&order)
    }
}
