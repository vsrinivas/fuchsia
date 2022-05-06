// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem;

use renderer::{Renderer, Timings, TILE_HEIGHT, TILE_WIDTH};
use rustc_hash::FxHashMap;
use surpass::{
    painter::{Color, Fill, Func, Style},
    rasterizer::Rasterizer,
    Order,
};

use crate::{Composition, Layer};

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
pub struct GpuRenderer {
    pub(crate) rasterizer: Rasterizer<TILE_WIDTH, TILE_HEIGHT>,
    pub(crate) style_map: StyleMap,
    pub(crate) renderer: Renderer,
}

impl GpuRenderer {
    pub fn new(
        device: &wgpu::Device,
        swap_chain_format: wgpu::TextureFormat,
        has_timestamp_query: bool,
    ) -> Self {
        Self {
            rasterizer: Rasterizer::default(),
            style_map: StyleMap::default(),
            renderer: Renderer::new(device, swap_chain_format, has_timestamp_query),
        }
    }

    pub fn render(
        &mut self,
        composition: &mut Composition,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        surface: &wgpu::Surface,
        width: u32,
        height: u32,
        clear_color: Color,
    ) -> Option<Timings> {
        composition.compact_geom();
        composition.shared_state.borrow_mut().props_interner.compact();

        let layers = &composition.layers;
        let rasterizer = &mut self.rasterizer;
        let shared_state = &mut *composition.shared_state.borrow_mut();
        let lines_builder = &mut shared_state.lines_builder;
        let geom_id_to_order = &shared_state.geom_id_to_order;
        let builder = lines_builder.take().expect("lines_builder should not be None");

        *lines_builder = {
            let lines = builder.build(|id| {
                geom_id_to_order
                    .get(&id)
                    .copied()
                    .flatten()
                    .and_then(|order| layers.get(&order))
                    .map(|layer| layer.inner.clone())
            });

            rasterizer.rasterize(&lines);

            Some(lines.unwrap())
        };

        self.style_map.populate(layers);

        self.renderer
            .render(
                device,
                queue,
                surface,
                width,
                height,
                unsafe { mem::transmute(rasterizer.segments()) },
                self.style_map.style_indices(),
                self.style_map.styles(),
                renderer::Color {
                    r: clear_color.r,
                    g: clear_color.g,
                    b: clear_color.b,
                    a: clear_color.a,
                },
            )
            .unwrap()
    }
}
