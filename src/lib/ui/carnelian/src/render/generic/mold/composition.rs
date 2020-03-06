// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, ops::RangeBounds, rc::Rc};

use crate::{
    render::generic::{mold::Mold, Composition, Layer},
    Color,
};

#[derive(Clone, Debug)]
pub struct MoldComposition {
    pub(crate) layers: Vec<Layer<Mold>>,
    pub(crate) old_layer_ids: Rc<RefCell<Vec<mold_next::LayerId>>>,
    pub(crate) background_color: Color,
}

impl Composition<Mold> for MoldComposition {
    fn new(background_color: Color) -> Self {
        Self { layers: vec![], old_layer_ids: Rc::new(RefCell::new(vec![])), background_color }
    }

    fn with_layers(layers: impl IntoIterator<Item = Layer<Mold>>, background_color: Color) -> Self {
        Self {
            layers: layers.into_iter().collect(),
            old_layer_ids: Rc::new(RefCell::new(vec![])),
            background_color,
        }
    }

    fn clear(&mut self) {
        let mut old_layer_ids = self.old_layer_ids.borrow_mut();
        old_layer_ids.extend(self.layers.drain(..).filter_map(|layer| layer.raster.layer_id.get()));
    }

    fn replace<R, I>(&mut self, range: R, with: I)
    where
        R: RangeBounds<usize>,
        I: IntoIterator<Item = Layer<Mold>>,
    {
        let mut old_layer_ids = self.old_layer_ids.borrow_mut();
        old_layer_ids.extend(
            self.layers.splice(range, with).filter_map(|layer| layer.raster.layer_id.get()),
        );
    }
}
