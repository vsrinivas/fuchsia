// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    collections::{HashMap, HashSet},
    sync::Arc,
};

use etagere::{size2, AllocId, Allocation, AtlasAllocator};
use renderer::{Renderer, Timings};
use rustc_hash::FxHashMap;
use surpass::{
    painter::{Color, Fill, Func, GradientType, Image, ImageId, Props},
    Order,
};

use crate::{Composition, Layer};

struct ImageAllocator {
    allocator: AtlasAllocator,
    image_to_alloc: HashMap<ImageId, Allocation>,
    unused_allocs: HashSet<AllocId>,
    new_allocs: Vec<(Arc<Image>, [u32; 4])>,
}

pub struct StyleMap {
    // Position of the style in the u32 buffer, by order value.
    style_offsets: Vec<u32>,
    styles: Vec<u32>,
    image_allocator: ImageAllocator,
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
                    (current >> (u32::BITS - B)) == 0,
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

        prepend::<16>(header, self.gradient_stop_count)
    }
}

impl ImageAllocator {
    fn new() -> ImageAllocator {
        ImageAllocator {
            allocator: AtlasAllocator::new(size2(4096, 4096)),
            image_to_alloc: HashMap::new(),
            unused_allocs: HashSet::new(),
            new_allocs: Vec::new(),
        }
    }

    fn start_populate(&mut self) {
        self.new_allocs.clear();
        self.unused_allocs = self.allocator.iter().map(|alloc| alloc.id).collect();
    }

    fn end_populate(&mut self) {
        for alloc_id in &self.unused_allocs {
            self.allocator.deallocate(*alloc_id);
        }
    }

    fn get_or_create_alloc(&mut self, image: &Arc<Image>) -> &Allocation {
        let new_allocs = &mut self.new_allocs;
        let allocator = &mut self.allocator;
        let allocation = self.image_to_alloc.entry(image.id()).or_insert_with(|| {
            let allocation = allocator
                .allocate(size2(image.width() as i32, image.height() as i32))
                .expect("Texture does not fit in the atlas");

            let rect = allocation.rectangle.to_u32();
            new_allocs.push((
                image.clone(),
                [rect.min.x, rect.min.y, rect.min.x + image.width(), rect.min.y + image.height()],
            ));
            allocation
        });
        self.unused_allocs.remove(&allocation.id);
        allocation
    }
}

impl StyleMap {
    fn new() -> StyleMap {
        StyleMap {
            style_offsets: Vec::new(),
            styles: Vec::new(),
            image_allocator: ImageAllocator::new(),
        }
    }

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
                is_clipped: u32::from(style.is_clipped),
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
            Fill::Texture(texture) => {
                self.styles.extend(texture.transform.to_array().map(f32::to_bits));

                let image = &texture.image;
                let alloc = self.image_allocator.get_or_create_alloc(image);

                let min = alloc.rectangle.min.cast::<f32>();
                self.styles.extend(
                    [min.x, min.y, min.x + image.width() as f32, min.y + image.height() as f32]
                        .map(f32::to_bits),
                );
            }
        }
    }

    pub fn populate(&mut self, layers: &FxHashMap<Order, Layer>) {
        self.style_offsets.clear();
        self.styles.clear();
        self.image_allocator.start_populate();

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

        self.image_allocator.end_populate();
    }

    pub fn style_offsets(&self) -> &[u32] {
        &self.style_offsets
    }

    pub fn styles(&self) -> &[u32] {
        &self.styles
    }

    pub fn new_allocs(&self) -> &[(Arc<Image>, [u32; 4])] {
        self.image_allocator.new_allocs.as_ref()
    }
}

pub struct GpuRenderer {
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
            style_map: StyleMap::new(),
            renderer: Renderer::new(device, swap_chain_format, has_timestamp_query),
        }
    }

    #[allow(clippy::too_many_arguments)]
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
        let frame = surface.get_current_texture().unwrap();
        let timings = self.render_to_texture(
            composition,
            device,
            queue,
            &frame.texture,
            width,
            height,
            clear_color,
        );
        frame.present();
        timings
    }

    #[allow(clippy::too_many_arguments)]
    pub fn render_to_texture(
        &mut self,
        composition: &mut Composition,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        texture: &wgpu::Texture,
        width: u32,
        height: u32,
        clear_color: Color,
    ) -> Option<Timings> {
        composition.compact_geom();
        composition.shared_state.borrow_mut().props_interner.compact();

        let layers = &composition.layers;
        let shared_state = &mut *composition.shared_state.borrow_mut();
        let lines_builder = &mut shared_state.lines_builder;
        let geom_id_to_order = &shared_state.geom_id_to_order;
        let builder = lines_builder.take().expect("lines_builder should not be None");
        let lines = builder.build_for_gpu(|id| {
            geom_id_to_order
                .get(&id)
                .copied()
                .flatten()
                .and_then(|order| layers.get(&order))
                .map(|layer| layer.inner.clone())
        });

        self.style_map.populate(layers);

        let timings = self
            .renderer
            .render(
                device,
                queue,
                texture,
                width,
                height,
                &lines,
                self.style_map.style_offsets(),
                self.style_map.styles(),
                self.style_map.new_allocs(),
                renderer::Color {
                    r: clear_color.r,
                    g: clear_color.g,
                    b: clear_color.b,
                    a: clear_color.a,
                },
            )
            .unwrap();

        *lines_builder = Some(lines.unwrap());

        timings
    }
}
