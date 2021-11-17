// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{borrow::Cow, cell::RefCell, mem, rc::Rc};

use rustc_hash::FxHashMap;
use surpass::{
    self,
    painter::{BufferLayout, LayerProps, Props, Rect},
    rasterizer::{self, Rasterizer},
    LinesBuilder,
};

use crate::{
    buffer::{Buffer, BufferLayerCache},
    layer::{IdSet, Layer, SmallBitSet},
    path::{Path, PathSegments},
};

const LINES_GARBAGE_THRESHOLD: usize = 2;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct LayerId(usize);

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
    layers: FxHashMap<u32, Layer>,
    layer_ids: IdSet,
    external_count: usize,
    external_to_internal: FxHashMap<LayerId, u32>,
    orders_to_layers: FxHashMap<u32, u32>,
    layouts: FxHashMap<(*mut [u8; 4], usize), BufferLayout>,
    buffers_with_caches: Rc<RefCell<SmallBitSet>>,
}

impl Composition {
    #[inline]
    pub fn new() -> Self {
        Self {
            builder: Some(LinesBuilder::new()),
            rasterizer: Rasterizer::new(),
            layers: FxHashMap::default(),
            layer_ids: IdSet::new(),
            external_count: 0,
            external_to_internal: FxHashMap::default(),
            orders_to_layers: FxHashMap::default(),
            layouts: FxHashMap::default(),
            buffers_with_caches: Rc::new(RefCell::new(SmallBitSet::default())),
        }
    }

    fn builder(&mut self) -> &mut LinesBuilder {
        self.builder.as_mut().expect("Composition::builder should not be None")
    }

    pub fn create_layer(&mut self) -> LayerId {
        let count = self.external_count;
        LayerId(mem::replace(&mut self.external_count, count + 1))
    }

    fn insert_segments(
        &mut self,
        layer_id: LayerId,
        segments: PathSegments<'_>,
    ) -> Option<&mut Layer> {
        let id = self.external_to_internal.get(&layer_id).copied().or_else(|| {
            let id = self.layer_ids.acquire();

            if id.is_none() {
                self.remove_disabled();
            }

            let id = id.or_else(|| {
                self.remove_disabled();
                self.layer_ids.acquire()
            })?;

            self.external_to_internal.insert(layer_id, id);

            Some(id)
        })?;

        let mut len = 0;
        for segment in segments {
            self.builder().push(
                id,
                &surpass::Segment::new(
                    surpass::Point::new(segment.p0.x, segment.p0.y),
                    surpass::Point::new(segment.p1.x, segment.p1.y),
                ),
            );

            len += 1;
        }

        let layer = self.layers.entry(id).or_default();

        layer.inner.order = Some(id);
        layer.inner.is_enabled = true;
        layer.len += len;

        Some(layer)
    }

    #[inline]
    pub fn insert_in_layer(&mut self, layer_id: LayerId, path: &Path) -> Option<&mut Layer> {
        self.insert_segments(layer_id, path.segments())
    }

    #[inline]
    pub fn insert_in_layer_transformed(
        &mut self,
        layer_id: LayerId,
        path: &Path,
        transform: &[f32; 9],
    ) -> Option<&mut Layer> {
        self.insert_segments(layer_id, path.transformed(transform))
    }

    #[inline]
    pub fn get(&self, layer_id: LayerId) -> Option<&Layer> {
        self.external_to_internal.get(&layer_id).and_then(|id| self.layers.get(id))
    }

    #[inline]
    pub fn get_mut(&mut self, layer_id: LayerId) -> Option<&mut Layer> {
        // TODO: remove with 2021 edition.
        let layers = &mut self.layers;
        self.external_to_internal.get(&layer_id).and_then(move |id| layers.get_mut(id))
    }

    #[inline]
    pub fn layers(&self) -> impl Iterator<Item = &Layer> {
        self.layers.values()
    }

    #[inline]
    pub fn layers_mut(&mut self) -> impl Iterator<Item = &mut Layer> {
        self.layers.values_mut()
    }

    fn actual_len(&self) -> usize {
        self.layers
            .values()
            .filter_map(|layer| if layer.inner.is_enabled { Some(layer.len) } else { None })
            .sum()
    }

    #[inline]
    pub fn remove_disabled(&mut self) {
        if self.builder().len() >= self.actual_len() * LINES_GARBAGE_THRESHOLD {
            take_builder!(self, |mut builder: LinesBuilder| {
                builder.retain(|layer| {
                    self.layers.get(&layer).map(|layer| layer.inner.is_enabled).unwrap_or_default()
                });
                builder
            });

            let layer_ids = &mut self.layer_ids;
            self.layers.retain(|&layer_id, layer| {
                if !layer.inner.is_enabled {
                    layer_ids.release(layer_id);
                }

                layer.inner.is_enabled
            });

            let layers = &mut self.layers;
            self.external_to_internal.retain(|_, id| layers.contains_key(id));
        }
    }

    #[inline]
    pub fn create_buffer_layer_cache(&mut self) -> Option<BufferLayerCache> {
        self.buffers_with_caches.borrow_mut().first_empty_slot().map(|id| BufferLayerCache {
            id,
            layers_per_tile: Default::default(),
            buffers_with_caches: Rc::downgrade(&self.buffers_with_caches),
        })
    }

    pub fn render(
        &mut self,
        mut buffer: Buffer<'_>,
        background_color: [f32; 4],
        crop: Option<Rect>,
    ) {
        if let Some(buffer_layer_cache) = buffer.layer_cache.as_ref() {
            let tiles_len = buffer.tiles_len();

            if buffer_layer_cache.layers_per_tile.borrow().len() != tiles_len {
                buffer_layer_cache.layers_per_tile.borrow_mut().resize(tiles_len, None);
                buffer_layer_cache.clear();
            }
        }

        self.remove_disabled();

        for (layer_id, layer) in &self.layers {
            if layer.inner.is_enabled {
                self.orders_to_layers.insert(
                    layer.inner.order.expect("Layers should always have orders"),
                    *layer_id,
                );
            }
        }

        let layout = self
            .layouts
            .entry((buffer.buffer.as_mut_ptr(), buffer.buffer.len()))
            .or_insert_with(|| buffer.generate_layout());

        let layers = &self.layers;
        let orders_to_layers = &self.orders_to_layers;
        let rasterizer = &mut self.rasterizer;

        struct CompositionContext<'l> {
            layers: &'l FxHashMap<u32, Layer>,
            orders_to_layers: &'l FxHashMap<u32, u32>,
            cache_id: Option<u8>,
        }

        impl LayerProps for CompositionContext<'_> {
            #[inline]
            fn get(&self, layer_id: u32) -> Cow<'_, Props> {
                let layer_id = self
                    .orders_to_layers
                    .get(&layer_id)
                    .expect("orders_to_layers was not populated in Composition::render");
                Cow::Borrowed(
                    self.layers
                        .get(layer_id)
                        .map(|layer| layer.props())
                        .expect("orders_to_layers points to non-existant Layer"),
                )
            }

            #[inline]
            fn is_unchanged(&self, layer_id: u32) -> bool {
                match self.cache_id {
                    None => return false,
                    Some(cache_id) => {
                        let layer_id = self
                            .orders_to_layers
                            .get(&layer_id)
                            .expect("orders_to_layers was not populated in Composition::render");
                        self.layers
                            .get(layer_id)
                            .map(|layer| layer.is_unchanged(cache_id))
                            .expect("orders_to_layers points to non-existant Layer")
                    }
                }
            }
        }

        let context = CompositionContext {
            layers,
            orders_to_layers,
            cache_id: buffer.layer_cache.as_ref().map(|cache| cache.id),
        };

        take_builder!(self, |builder: LinesBuilder| {
            let lines =
                builder.build(|layer_id| layers.get(&layer_id).map(|layer| layer.inner.clone()));

            rasterizer.rasterize(&lines);
            rasterizer.sort();

            let last_segment =
                rasterizer::search_last_by_key(rasterizer.segments(), false, |segment| {
                    segment.is_none()
                })
                .unwrap_or(0);
            let segments = rasterizer.segments().get(0..=last_segment).unwrap_or(&[]);
            let mut layers_per_tile = buffer
                .layer_cache
                .as_ref()
                .map(|buffer_layer_cache| buffer_layer_cache.layers_per_tile.borrow_mut());

            layout.print(
                &mut buffer.buffer,
                layers_per_tile.as_mut().map(|layers_per_tile| layers_per_tile.as_mut_slice()),
                buffer.flusher.as_ref().map(|flusher| &**flusher),
                segments,
                background_color,
                crop,
                context,
            );

            lines.unwrap()
        });

        if let Some(buffer_layer_cache) = buffer.layer_cache {
            for layer in self.layers.values_mut() {
                layer.set_is_unchanged(buffer_layer_cache.id, layer.inner.is_enabled);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::convert::TryFrom;

    use surpass::TILE_SIZE;

    use crate::{layer::AffineTransform, Fill, FillRule, Func, Point, Style};

    const BLACK: [u8; 4] = [0x00, 0x0, 0x00, 0xFF];
    const BLACKF: [f32; 4] = [0.0, 0.0, 0.0, 1.0];
    const RED: [u8; 4] = [0xFF, 0x0, 0x00, 0xFF];
    const REDF: [f32; 4] = [1.0, 0.0, 0.0, 1.0];
    const GREEN: [u8; 4] = [0x00, 0xFF, 0x00, 0xFF];
    const GREENF: [f32; 4] = [0.0, 1.0, 0.0, 1.0];
    const RED_GREEN_50: [u8; 4] = [0xBB, 0xBB, 0x00, 0xFF];

    fn pixel_path(x: i32, y: i32) -> Path {
        let mut path = Path::new();

        path.line(Point::new(x as f32, y as f32), Point::new(x as f32, (y + 1) as f32));
        path.line(Point::new(x as f32, (y + 1) as f32), Point::new((x + 1) as f32, (y + 1) as f32));
        path.line(Point::new((x + 1) as f32, (y + 1) as f32), Point::new((x + 1) as f32, y as f32));
        path.line(Point::new((x + 1) as f32, y as f32), Point::new(x as f32, y as f32));

        path
    }

    fn solid(color: [f32; 4]) -> Props {
        Props {
            func: Func::Draw(Style { fill: Fill::Solid(color), ..Default::default() }),
            ..Default::default()
        }
    }

    #[test]
    fn background_color_clear() {
        let mut buffer = [GREEN];
        let mut composition = Composition::new();

        composition.render(
            Buffer { buffer: &mut buffer, width: 1, ..Default::default() },
            REDF,
            None,
        );

        assert_eq!(buffer, [RED]);
    }

    #[test]
    fn one_pixel() {
        let mut buffer = [GREEN; 3];
        let mut composition = Composition::new();

        let layer_id = composition.create_layer();
        composition.insert_in_layer(layer_id, &pixel_path(1, 0)).unwrap().set_props(solid(REDF));

        composition.render(
            Buffer { buffer: &mut buffer, width: 3, ..Default::default() },
            GREENF,
            None,
        );

        assert_eq!(buffer, [GREEN, RED, GREEN]);
    }

    #[test]
    fn two_pixels_same_layer() {
        let mut buffer = [GREEN; 3];
        let mut composition = Composition::new();

        let layer_id = composition.create_layer();
        composition.insert_in_layer(layer_id, &pixel_path(1, 0)).unwrap().set_props(solid(REDF));
        composition.insert_in_layer(layer_id, &pixel_path(2, 0)).unwrap();

        composition.render(
            Buffer { buffer: &mut buffer, width: 3, ..Default::default() },
            GREENF,
            None,
        );

        assert_eq!(buffer, [GREEN, RED, RED]);
    }

    #[test]
    fn one_pixel_translated() {
        let mut buffer = [GREEN; 3];
        let mut composition = Composition::new();

        let layer_id = composition.create_layer();
        composition
            .insert_in_layer(layer_id, &pixel_path(1, 0))
            .unwrap()
            .set_props(solid(REDF))
            .set_transform(AffineTransform::try_from([1.0, 0.0, 0.0, 1.0, 0.5, 0.0]).unwrap());

        composition.render(
            Buffer { buffer: &mut buffer, width: 3, ..Default::default() },
            GREENF,
            None,
        );

        assert_eq!(buffer, [GREEN, RED_GREEN_50, RED_GREEN_50]);
    }

    #[test]
    fn one_pixel_rotated() {
        let mut buffer = [GREEN; 3];
        let mut composition = Composition::new();
        let angle = -std::f32::consts::PI / 2.0;

        let layer_id = composition.create_layer();
        composition
            .insert_in_layer(layer_id, &pixel_path(-1, 1))
            .unwrap()
            .set_props(solid(REDF))
            .set_transform(
                AffineTransform::try_from([
                    angle.cos(),
                    -angle.sin(),
                    angle.sin(),
                    angle.cos(),
                    0.0,
                    0.0,
                ])
                .unwrap(),
            );

        composition.render(
            Buffer { buffer: &mut buffer, width: 3, ..Default::default() },
            GREENF,
            None,
        );

        assert_eq!(buffer, [GREEN, RED, GREEN]);
    }

    #[test]
    fn remove_and_resize() {
        let mut buffer = [GREEN; 4];
        let mut composition = Composition::new();

        let layer_id0 = composition.create_layer();
        let layer_id1 = composition.create_layer();
        let layer_id2 = composition.create_layer();
        composition.insert_in_layer(layer_id0, &pixel_path(0, 0)).unwrap().set_props(solid(REDF));
        composition.insert_in_layer(layer_id1, &pixel_path(1, 0)).unwrap().set_props(solid(REDF));
        composition.insert_in_layer(layer_id2, &pixel_path(2, 0)).unwrap().set_props(solid(REDF));
        composition.insert_in_layer(layer_id2, &pixel_path(3, 0)).unwrap().set_props(solid(REDF));

        composition.render(
            Buffer { buffer: &mut buffer, width: 4, ..Default::default() },
            GREENF,
            None,
        );

        assert_eq!(buffer, [RED, RED, RED, RED]);
        assert_eq!(composition.builder().len(), 16);
        assert_eq!(composition.actual_len(), 16);

        buffer = [GREEN; 4];

        composition.get_mut(layer_id0).unwrap().disable();

        composition.render(
            Buffer { buffer: &mut buffer, width: 3, ..Default::default() },
            GREENF,
            None,
        );

        assert_eq!(buffer, [GREEN, RED, RED, RED]);
        assert_eq!(composition.builder().len(), 16);
        assert_eq!(composition.actual_len(), 12);

        buffer = [GREEN; 4];

        composition.get_mut(layer_id2).unwrap().disable();

        composition.render(
            Buffer { buffer: &mut buffer, width: 3, ..Default::default() },
            GREENF,
            None,
        );

        assert_eq!(buffer, [GREEN, RED, GREEN, GREEN]);
        assert_eq!(composition.builder().len(), 4);
        assert_eq!(composition.actual_len(), 4);
    }

    #[test]
    fn remove_twice() {
        let mut composition = Composition::new();

        let layer_id = composition.create_layer();
        composition.insert_in_layer(layer_id, &pixel_path(0, 0)).unwrap().set_props(solid(REDF));

        assert_eq!(composition.actual_len(), 4);

        composition.get_mut(layer_id).unwrap().disable();

        assert_eq!(composition.actual_len(), 0);

        composition.get_mut(layer_id).unwrap().disable();

        assert_eq!(composition.actual_len(), 0);
    }

    #[test]
    fn render_change_layers_only() {
        let mut buffer = [BLACK; 3 * TILE_SIZE * TILE_SIZE];
        let mut composition = Composition::new();
        let layer_cache = composition.create_buffer_layer_cache();

        let layer_id = composition.create_layer();
        composition.insert_in_layer(layer_id, &pixel_path(0, 0)).unwrap().set_props(solid(REDF));
        composition.insert_in_layer(layer_id, &pixel_path(TILE_SIZE as i32, 0)).unwrap();

        let layer_id = composition.create_layer();
        composition
            .insert_in_layer(layer_id, &pixel_path(TILE_SIZE as i32 + 1, 0))
            .unwrap()
            .set_props(solid(GREENF));
        composition.insert_in_layer(layer_id, &pixel_path(2 * TILE_SIZE as i32, 0)).unwrap();

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: 3 * TILE_SIZE,
                layer_cache: layer_cache.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], RED);
        assert_eq!(buffer[TILE_SIZE], RED);
        assert_eq!(buffer[TILE_SIZE + 1], GREEN);
        assert_eq!(buffer[2 * TILE_SIZE], GREEN);

        buffer.fill(BLACK);

        composition.get_mut(layer_id).unwrap().set_props(solid(REDF));

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: 3 * TILE_SIZE,
                layer_cache: layer_cache.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], BLACK);
        assert_eq!(buffer[TILE_SIZE], RED);
        assert_eq!(buffer[TILE_SIZE + 1], RED);
        assert_eq!(buffer[2 * TILE_SIZE], RED);
    }

    #[test]
    fn clear_emptied_tiles() {
        let mut buffer = [BLACK; 2 * TILE_SIZE * TILE_SIZE];
        let mut composition = Composition::new();
        let layer_cache = composition.create_buffer_layer_cache();

        let layer_id = composition.create_layer();
        composition.insert_in_layer(layer_id, &pixel_path(0, 0)).unwrap().set_props(solid(REDF));
        composition.insert_in_layer(layer_id, &pixel_path(TILE_SIZE as i32, 0)).unwrap();

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: 2 * TILE_SIZE,
                layer_cache: layer_cache.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], RED);

        composition.get_mut(layer_id).unwrap().set_transform(
            AffineTransform::try_from([1.0, 0.0, 0.0, 1.0, TILE_SIZE as f32, 0.0]).unwrap(),
        );

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: 2 * TILE_SIZE,
                layer_cache: layer_cache.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], BLACK);

        composition.get_mut(layer_id).unwrap().set_transform(
            AffineTransform::try_from([1.0, 0.0, 0.0, 1.0, -(TILE_SIZE as f32), 0.0]).unwrap(),
        );

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: TILE_SIZE,
                layer_cache: layer_cache.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], RED);

        composition.get_mut(layer_id).unwrap().set_transform(
            AffineTransform::try_from([1.0, 0.0, 0.0, 1.0, 0.0, TILE_SIZE as f32]).unwrap(),
        );

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: TILE_SIZE,
                layer_cache: layer_cache.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], BLACK);
    }

    #[test]
    fn separare_layer_caches() {
        let mut buffer = [BLACK; TILE_SIZE * TILE_SIZE];
        let mut composition = Composition::new();
        let layer_cache0 = composition.create_buffer_layer_cache();
        let layer_cache1 = composition.create_buffer_layer_cache();

        let layer_id = composition.create_layer();
        composition.insert_in_layer(layer_id, &pixel_path(0, 0)).unwrap().set_props(solid(REDF));

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: TILE_SIZE,
                layer_cache: layer_cache0.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], RED);

        buffer.fill(BLACK);

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: TILE_SIZE,
                layer_cache: layer_cache0.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], BLACK);

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: TILE_SIZE,
                layer_cache: layer_cache1.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], RED);

        composition
            .get_mut(layer_id)
            .unwrap()
            .set_transform(AffineTransform::try_from([1.0, 0.0, 0.0, 1.0, 1.0, 0.0]).unwrap());

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: TILE_SIZE,
                layer_cache: layer_cache0.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], BLACK);
        assert_eq!(buffer[1], RED);

        buffer.fill(BLACK);

        composition.render(
            Buffer {
                buffer: &mut buffer,
                width: TILE_SIZE,
                layer_cache: layer_cache1.clone(),
                ..Default::default()
            },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], BLACK);
        assert_eq!(buffer[1], RED);
    }

    #[test]
    fn even_odd() {
        let mut path = Path::new();

        path.line(Point::new(0.0, 0.0), Point::new(0.0, TILE_SIZE as f32));
        path.line(
            Point::new(0.0, TILE_SIZE as f32),
            Point::new(3.0 * TILE_SIZE as f32, TILE_SIZE as f32),
        );
        path.line(
            Point::new(3.0 * TILE_SIZE as f32, TILE_SIZE as f32),
            Point::new(3.0 * TILE_SIZE as f32, 0.0),
        );
        path.line(Point::new(3.0 * TILE_SIZE as f32, 0.0), Point::new(TILE_SIZE as f32, 0.0));
        path.line(
            Point::new(TILE_SIZE as f32, 0.0),
            Point::new(TILE_SIZE as f32, TILE_SIZE as f32),
        );
        path.line(
            Point::new(TILE_SIZE as f32, TILE_SIZE as f32),
            Point::new(2.0 * TILE_SIZE as f32, TILE_SIZE as f32),
        );
        path.line(
            Point::new(2.0 * TILE_SIZE as f32, TILE_SIZE as f32),
            Point::new(2.0 * TILE_SIZE as f32, 0.0),
        );
        path.line(Point::new(2.0 * TILE_SIZE as f32, 0.0), Point::new(0.0, 0.0));

        let mut buffer = [BLACK; 3 * TILE_SIZE * TILE_SIZE];
        let mut composition = Composition::new();

        let layer_id = composition.create_layer();
        composition.insert_in_layer(layer_id, &path).unwrap().set_props(Props {
            fill_rule: FillRule::EvenOdd,
            func: Func::Draw(Style { fill: Fill::Solid(REDF), ..Default::default() }),
        });

        composition.render(
            Buffer { buffer: &mut buffer, width: 3 * TILE_SIZE, ..Default::default() },
            BLACKF,
            None,
        );

        assert_eq!(buffer[0], RED);
        assert_eq!(buffer[TILE_SIZE], BLACK);
        assert_eq!(buffer[2 * TILE_SIZE], RED);
    }
}
