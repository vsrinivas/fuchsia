// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem;

use renderer::{Renderer, Timings, TILE_HEIGHT, TILE_WIDTH};
use rustc_hash::FxHashMap;
use surpass::{
    painter::{Color, Fill, Func, GradientType, Props},
    rasterizer::Rasterizer,
    Order,
};

use crate::{Composition, Layer};

#[derive(Debug, Default)]
pub struct StyleMap {
    // Position of the style in the u32 buffer, by order value.
    style_offsets: Vec<u32>,
    styles: Vec<u32>,
}

#[derive(Default)]
struct PropHeader {
    func: u32,
    is_clipped: u32,
    fill_rule: u32,
    fill_type: u32,
    blend_mode: u32,
    gradient_stop_count: u32,
}

impl PropHeader {
    fn to_bits(&self) -> u32 {
        fn prepend<const B: u32>(current: u32, value: u32) -> u32 {
            #[cfg(test)]
            {
                assert!((value >> B) == 0, "Unable to store {} with {} bits", value, B);
                assert!(
                    (current >> u32::BITS - B) == 0,
                    "Prepending {} bit would drop the mos significan bits of {}",
                    B,
                    value
                );
            }
            (current << B) + value
        }
        let header = 0;
        let header = prepend::<1>(header, self.func);
        let header = prepend::<1>(header, self.is_clipped);
        let header = prepend::<1>(header, self.fill_rule);
        let header = prepend::<2>(header, self.fill_type);
        let header = prepend::<4>(header, self.blend_mode);
        let header = prepend::<16>(header, self.gradient_stop_count);
        header
    }
}

impl StyleMap {
    fn push(&mut self, props: &Props) {
        let style = match &props.func {
            Func::Clip(order) => {
                self.styles.push(
                    PropHeader { func: 1, fill_rule: props.fill_rule as u32, ..Default::default() }
                        .to_bits(),
                );
                self.styles.push(*order as u32);
                return;
            }
            Func::Draw(style) => style,
        };
        self.styles.push(
            PropHeader {
                func: 0,
                fill_rule: props.fill_rule as u32,
                is_clipped: style.is_clipped as u32,
                fill_type: match &style.fill {
                    Fill::Solid(_) => 0,
                    Fill::Gradient(gradient) => match gradient.r#type() {
                        GradientType::Linear => 1,
                        GradientType::Radial => 2,
                    },
                    Fill::Texture(_) => 3,
                },
                blend_mode: style.blend_mode as u32,
                gradient_stop_count: match &style.fill {
                    Fill::Gradient(gradient) => gradient.colors_with_stops().len() as u32,
                    _ => 0,
                },
            }
            .to_bits(),
        );

        match &style.fill {
            Fill::Solid(color) => self.styles.extend(color.to_array().map(f32::to_bits)),
            Fill::Gradient(gradient) => {
                self.styles.extend(gradient.start().to_array().map(f32::to_bits));
                self.styles.extend(gradient.end().to_array().map(f32::to_bits));
                gradient.colors_with_stops().iter().for_each(|(color, stop)| {
                    self.styles.extend(color.to_array().map(f32::to_bits));
                    self.styles.push(stop.to_bits());
                });
            }
            _ => {}
        }
    }

    pub fn populate(&mut self, layers: &FxHashMap<Order, Layer>) {
        self.style_offsets.clear();
        self.styles.clear();

        let mut props_set = FxHashMap::default();

        for (order, layer) in layers.iter() {
            let order = order.as_u32();
            let props = layer.props();

            if self.style_offsets.len() <= order as usize {
                self.style_offsets.resize(order as usize + 1, 0);
            }

            let offset = *props_set.entry(props).or_insert_with(|| {
                let offset = self.styles.len() as u32;
                self.push(props);
                offset
            });

            self.style_offsets[order as usize] = offset;
        }
    }

    pub fn style_offsets(&self) -> &[u32] {
        &self.style_offsets
    }

    pub fn styles(&self) -> &[u32] {
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
                self.style_map.style_offsets(),
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
