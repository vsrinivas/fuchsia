// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use renderer::{Renderer, TILE_HEIGHT, TILE_WIDTH};
use rustc_hash::FxHashMap;
use surpass::{
    painter::{Fill, Func, Style},
    rasterizer::Rasterizer,
    Order,
};

use crate::Layer;

use super::Backend;

#[derive(Debug, Default)]
pub struct StyleMap {
    style_indices: Vec<u32>,
    styles: Vec<renderer::Style>,
}

impl StyleMap {
    pub fn populate(&mut self, layers: &FxHashMap<Order, Layer>) {
        self.style_indices.clear();
        self.styles.clear();

        let mut props_set = FxHashMap::default();

        for (order, layer) in layers.iter() {
            let order = order.as_u32();
            let props = layer.props();

            if self.style_indices.len() <= order as usize {
                self.style_indices.resize(order as usize + 1, 0);
            }

            let styles = &mut self.styles;
            let index = *props_set.entry(props).or_insert_with(|| {
                let index = styles.len() as u32;

                styles.push(renderer::Style {
                    fill_rule: props.fill_rule as u32,
                    color: match props.func {
                        Func::Draw(Style { fill: Fill::Solid(color), .. }) => {
                            renderer::Color { r: color.r, g: color.g, b: color.b, a: color.a }
                        }
                        Func::Draw(Style { fill: Fill::Gradient(ref gradient), .. }) => {
                            let color = gradient.colors_with_stops()[0].0;
                            renderer::Color { r: color.r, g: color.g, b: color.b, a: color.a }
                        }
                        Func::Clip(_) => renderer::Color { r: 0.0, g: 0.0, b: 0.0, a: 0.0 },
                        _ => renderer::Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
                    },
                    blend_mode: match props.func {
                        Func::Draw(Style { blend_mode, .. }) => blend_mode as u32,
                        _ => 0,
                    },
                });

                index
            });

            self.style_indices[order as usize] = index;
        }
    }

    pub fn style_indices(&self) -> &[u32] {
        &self.style_indices
    }

    pub fn styles(&self) -> &[renderer::Style] {
        &self.styles
    }
}

#[derive(Debug)]
pub struct GpuBackend {
    pub(crate) rasterizer: Rasterizer<TILE_WIDTH, TILE_HEIGHT>,
    pub(crate) style_map: StyleMap,
    pub(crate) renderer: Renderer,
}

impl Backend for GpuBackend {}
