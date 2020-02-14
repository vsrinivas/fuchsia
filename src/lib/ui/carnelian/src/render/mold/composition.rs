// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{ops::RangeBounds, rc::Rc, vec::Splice};

use crate::{
    render::{mold::Mold, BlendMode, Composition, Fill, FillRule, Layer, Style},
    Color,
};

fn style_to_ops(style: &Style) -> Vec<mold::tile::Op> {
    let fill_rule = match style.fill_rule {
        FillRule::NonZero => vec![mold::tile::Op::CoverWipZero, mold::tile::Op::CoverWipNonZero],
        FillRule::EvenOdd => vec![mold::tile::Op::CoverWipZero, mold::tile::Op::CoverWipEvenOdd],
        FillRule::WholeTile => vec![mold::tile::Op::CoverWipZero],
    };
    let fill = match style.fill {
        Fill::Solid(color) => mold::tile::Op::ColorWipFillSolid(u32::from_be_bytes([
            color.r, color.g, color.b, color.a,
        ])),
    };
    let blend_mode = match style.blend_mode {
        BlendMode::Over => mold::tile::Op::ColorAccBlendOver,
    };

    fill_rule.into_iter().chain(std::iter::once(fill)).chain(std::iter::once(blend_mode)).collect()
}

#[derive(Clone, Debug)]
pub struct MoldComposition {
    layers: Rc<Vec<Layer<Mold>>>,
    background_color: Color,
}

impl MoldComposition {
    pub(crate) fn set_up_map(&self, map: &mut mold::tile::Map) -> u32 {
        map.global(0, vec![mold::tile::Op::ColorAccZero]);

        for (i, layer) in self.layers.iter().enumerate() {
            map.print(
                i as u32 + 1,
                mold::Layer::new(
                    mold::Raster::union(layer.raster.raster.iter()),
                    style_to_ops(&layer.style),
                ),
            );
        }

        let len = self.layers.len() as u32 + 1;
        map.global(
            len,
            vec![mold::tile::Op::ColorAccBackground(u32::from_be_bytes([
                self.background_color.r,
                self.background_color.g,
                self.background_color.b,
                self.background_color.a,
            ]))],
        );

        len
    }
}

impl Composition<Mold> for MoldComposition {
    fn new(layers: impl IntoIterator<Item = Layer<Mold>>, background_color: Color) -> Self {
        Self { layers: Rc::new(layers.into_iter().collect()), background_color }
    }

    fn splice<R, I>(&mut self, range: R, replace_with: I) -> Splice<'_, I::IntoIter>
    where
        R: RangeBounds<usize>,
        I: IntoIterator<Item = Layer<Mold>>,
    {
        Rc::get_mut(&mut self.layers).unwrap().splice(range, replace_with)
    }
}
