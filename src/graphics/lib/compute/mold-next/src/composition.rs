// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rustc_hash::FxHashMap;
use surpass::{
    self,
    painter::BufferLayout,
    rasterizer::{self, Rasterizer},
    LinesBuilder,
};

use crate::{
    buffer::Buffer,
    layer::{Layer, LayerId, LayerIdSet},
    path::{Path, PathSegments},
};

const LINES_GARBAGE_THRESHOLD: usize = 2;

macro_rules! take_builder {
    ( $slf:expr, $f:expr ) => {{
        let builder = $slf.builder.take().expect("Composition::builder should not be None");
        $slf.builder = Some($f(builder));
    }};
}

#[derive(Debug)]
pub struct Composition {
    builder: Option<LinesBuilder>,
    rasterizer: Rasterizer,
    actual_len: usize,
    layers: FxHashMap<u16, Layer>,
    layer_ids: LayerIdSet,
    orders_to_layers: FxHashMap<u16, u16>,
    lengths: FxHashMap<u16, usize>,
    layout: Option<BufferLayout>,
}

impl Composition {
    #[inline]
    pub fn new() -> Self {
        Self {
            builder: Some(LinesBuilder::new()),
            rasterizer: Rasterizer::new(),
            actual_len: 0,
            layers: FxHashMap::default(),
            layer_ids: LayerIdSet::new(),
            orders_to_layers: FxHashMap::default(),
            lengths: FxHashMap::default(),
            layout: None,
        }
    }

    fn builder(&mut self) -> &mut LinesBuilder {
        self.builder.as_mut().expect("Composition::builder should not be None")
    }

    fn try_compact(&mut self) {
        if self.builder().len() >= self.actual_len * LINES_GARBAGE_THRESHOLD {
            take_builder!(self, |mut builder: LinesBuilder| {
                builder.retain(|layer| {
                    self.layers.get(&layer).map(|layer| layer.inner.is_enabled).unwrap_or_default()
                });
                builder
            });

            self.layers.retain(|_, layer| layer.inner.is_enabled);
        }
    }

    pub fn create_layer(&mut self) -> Option<LayerId> {
        self.layer_ids.create_id()
    }

    fn insert_segments(&mut self, layer_id: LayerId, segments: PathSegments<'_>) -> &mut Layer {
        let mut len = 0;
        for segment in segments {
            self.builder().push(
                layer_id.0,
                &surpass::Segment::new(
                    surpass::Point::new(segment.p0.x, segment.p0.y),
                    surpass::Point::new(segment.p1.x, segment.p1.y),
                ),
            );

            len += 1;
        }

        self.actual_len += len;
        self.lengths.entry(layer_id.0).and_modify(|current_len| *current_len += len).or_insert(len);

        let layer = self.layers.entry(layer_id.0).or_default();
        layer.inner.order = Some(layer_id.0);
        layer
    }

    #[inline]
    pub fn insert_in_layer(&mut self, layer_id: LayerId, path: &Path) -> &mut Layer {
        self.insert_segments(layer_id, path.segments())
    }

    #[inline]
    pub fn insert_in_layer_transformed(
        &mut self,
        layer_id: LayerId,
        path: &Path,
        transform: &[f32; 9],
    ) -> &mut Layer {
        self.insert_segments(layer_id, path.transformed(transform))
    }

    #[inline]
    pub fn remove_layer(&mut self, layer_id: LayerId) {
        self.layer_ids.remove(layer_id);

        if let Some(layer) = self.layers.get_mut(&layer_id.0) {
            layer.inner.is_enabled = false;
        }

        if let Some(len) = self.lengths.remove(&layer_id.0) {
            self.actual_len -= len;
            self.try_compact();
        }
    }

    #[inline]
    pub fn get(&self, layer_id: LayerId) -> Option<&Layer> {
        self.layers.get(&layer_id.0)
    }

    #[inline]
    pub fn get_mut(&mut self, layer_id: LayerId) -> Option<&mut Layer> {
        self.layers.get_mut(&layer_id.0)
    }

    #[inline]
    pub fn layers(&self) -> impl Iterator<Item = &Layer> {
        self.layers.values()
    }

    #[inline]
    pub fn layers_mut(&mut self) -> impl Iterator<Item = &mut Layer> {
        self.layers.values_mut()
    }

    pub fn render(&mut self, mut buffer: Buffer<'_>, background_color: [u8; 4]) {
        for (layer_id, layer) in &self.layers {
            if layer.inner.is_enabled {
                self.orders_to_layers.insert(
                    layer.inner.order.expect("Layers should always have orders"),
                    *layer_id,
                );
            }
        }

        let layout = self.layout.get_or_insert_with(|| buffer.generate_layout());
        if !layout.same_buffer(buffer.buffer) {
            *layout = buffer.generate_layout();
        }

        let layers = &self.layers;
        let orders_to_layers = &self.orders_to_layers;
        let rasterizer = &mut self.rasterizer;

        take_builder!(self, |builder: LinesBuilder| {
            let lines =
                builder.build(|layer_id| layers.get(&layer_id).map(|layer| layer.inner.clone()));

            rasterizer.rasterize(&lines);
            rasterizer.sort();

            let last_segment =
                rasterizer::search_last_by_key(rasterizer.segments(), 0, |segment| {
                    segment.is_none()
                })
                .unwrap_or(0);
            let segments = rasterizer.segments().get(0..=last_segment).unwrap_or(&[]);

            layout.print(&mut buffer.buffer, segments, background_color, |order| {
                let layer_id = orders_to_layers
                    .get(&order)
                    .expect("orders_to_layers was not populated in Composition::render");
                layers
                    .get(layer_id)
                    .map(|layer| *layer.style())
                    .expect("orders_to_layers points to non-existant Layer")
            });

            lines.unwrap()
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{Fill, Point, Style};

    const RED: [u8; 4] = [0xFF, 0x0, 0x00, 0xFF];
    const GREEN: [u8; 4] = [0x00, 0xFF, 0x00, 0xFF];
    const RED_GREEN_50: [u8; 4] = [0x80, 0x7F, 0x00, 0xFF];

    fn pixel_path(x: i32, y: i32) -> Path {
        let mut path = Path::new();

        path.line(Point::new(x as f32, y as f32), Point::new(x as f32, (y + 1) as f32));
        path.line(Point::new(x as f32, (y + 1) as f32), Point::new((x + 1) as f32, (y + 1) as f32));
        path.line(Point::new((x + 1) as f32, (y + 1) as f32), Point::new((x + 1) as f32, y as f32));
        path.line(Point::new((x + 1) as f32, y as f32), Point::new(x as f32, y as f32));

        path
    }

    #[test]
    fn background_color_untouched() {
        let mut buffer = [GREEN];
        let mut composition = Composition::new();

        composition.render(Buffer { buffer: &mut buffer, width: 1, width_stride: None }, RED);

        assert_eq!(buffer, [GREEN]);
    }

    #[test]
    fn one_pixel() {
        let mut buffer = [GREEN; 3];
        let mut composition = Composition::new();

        let layer_id = composition.create_layer().unwrap();
        composition
            .insert_in_layer(layer_id, &pixel_path(1, 0))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() });

        composition.render(Buffer { buffer: &mut buffer, width: 3, width_stride: None }, GREEN);

        assert_eq!(buffer, [GREEN, RED, GREEN]);
    }

    #[test]
    fn two_pixels_same_layer() {
        let mut buffer = [GREEN; 3];
        let mut composition = Composition::new();

        let layer_id = composition.create_layer().unwrap();
        composition
            .insert_in_layer(layer_id, &pixel_path(1, 0))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() });
        composition.insert_in_layer(layer_id, &pixel_path(2, 0));

        composition.render(Buffer { buffer: &mut buffer, width: 3, width_stride: None }, GREEN);

        assert_eq!(buffer, [GREEN, RED, RED]);
    }

    #[test]
    fn one_pixel_translated() {
        let mut buffer = [GREEN; 3];
        let mut composition = Composition::new();

        let layer_id = composition.create_layer().unwrap();
        composition
            .insert_in_layer(layer_id, &pixel_path(1, 0))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() })
            .set_transform(&[1.0, 0.0, 0.0, 1.0, 0.5, 0.0]);

        composition.render(Buffer { buffer: &mut buffer, width: 3, width_stride: None }, GREEN);

        assert_eq!(buffer, [GREEN, RED_GREEN_50, RED_GREEN_50]);
    }

    #[test]
    fn one_pixel_rotated() {
        let mut buffer = [GREEN; 3];
        let mut composition = Composition::new();
        let angle = -std::f32::consts::PI / 2.0;

        let layer_id = composition.create_layer().unwrap();
        composition
            .insert_in_layer(layer_id, &pixel_path(-1, 1))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() })
            .set_transform(&[angle.cos(), -angle.sin(), angle.sin(), angle.cos(), 0.0, 0.0]);

        composition.render(Buffer { buffer: &mut buffer, width: 3, width_stride: None }, GREEN);

        assert_eq!(buffer, [GREEN, RED, GREEN]);
    }

    #[test]
    fn remove_and_resize() {
        let mut buffer = [GREEN; 4];
        let mut composition = Composition::new();

        let layer_id0 = composition.create_layer().unwrap();
        let layer_id1 = composition.create_layer().unwrap();
        let layer_id2 = composition.create_layer().unwrap();
        composition
            .insert_in_layer(layer_id0, &pixel_path(0, 0))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() });
        composition
            .insert_in_layer(layer_id1, &pixel_path(1, 0))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() });
        composition
            .insert_in_layer(layer_id2, &pixel_path(2, 0))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() });
        composition
            .insert_in_layer(layer_id2, &pixel_path(3, 0))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() });

        composition.render(Buffer { buffer: &mut buffer, width: 4, width_stride: None }, GREEN);

        assert_eq!(buffer, [RED, RED, RED, RED]);
        assert_eq!(composition.builder().len(), 16);
        assert_eq!(composition.actual_len, 16);

        buffer = [GREEN; 4];

        composition.remove_layer(layer_id0);

        composition.render(Buffer { buffer: &mut buffer, width: 3, width_stride: None }, GREEN);

        assert_eq!(buffer, [GREEN, RED, RED, RED]);
        assert_eq!(composition.builder().len(), 16);
        assert_eq!(composition.actual_len, 12);

        buffer = [GREEN; 4];

        composition.remove_layer(layer_id2);

        composition.render(Buffer { buffer: &mut buffer, width: 3, width_stride: None }, GREEN);

        assert_eq!(buffer, [GREEN, RED, GREEN, GREEN]);
        assert_eq!(composition.builder().len(), 4);
        assert_eq!(composition.actual_len, 4);
    }

    #[test]
    fn remove_twice() {
        let mut composition = Composition::new();

        let layer_id = composition.create_layer().unwrap();
        composition
            .insert_in_layer(layer_id, &pixel_path(0, 0))
            .set_style(Style { fill: Fill::Solid(RED), ..Default::default() });

        assert_eq!(composition.actual_len, 4);

        composition.remove_layer(layer_id);

        assert_eq!(composition.actual_len, 0);

        composition.remove_layer(layer_id);

        assert_eq!(composition.actual_len, 0);
    }
}
