// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::RangeBounds;

use crate::{
    render::generic::{mold::Mold, Composition, Layer},
    Color,
};

#[derive(Clone, Debug)]
pub struct MoldComposition {
    pub(crate) layers: Vec<Layer<Mold>>,
    pub(crate) background_color: Color,
}

impl Composition<Mold> for MoldComposition {
    fn new(background_color: Color) -> Self {
        Self { layers: vec![], background_color }
    }

    fn with_layers(layers: impl IntoIterator<Item = Layer<Mold>>, background_color: Color) -> Self {
        Self { layers: layers.into_iter().collect(), background_color }
    }

    fn clear(&mut self) {
        self.layers.clear();
    }

    fn replace<R, I>(&mut self, range: R, with: I)
    where
        R: RangeBounds<usize>,
        I: IntoIterator<Item = Layer<Mold>>,
    {
        self.layers.splice(range, with);
    }
}
