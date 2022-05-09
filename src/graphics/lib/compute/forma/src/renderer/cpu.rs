// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    borrow::Cow,
    cell::{RefCell, RefMut},
    rc::Rc,
};

use rustc_hash::FxHashMap;
use surpass::{
    layout::Layout,
    painter::{self, Channel, Color, LayerProps, Props, Rect},
    rasterizer::Rasterizer,
    Order, TILE_HEIGHT, TILE_WIDTH,
};

use crate::{
    buffer::{Buffer, BufferLayerCache},
    Composition, Layer,
};

use crate::small_bit_set::SmallBitSet;

#[derive(Debug, Default)]
pub struct CpuRenderer {
    rasterizer: Rasterizer<TILE_WIDTH, TILE_HEIGHT>,
    buffers_with_caches: Rc<RefCell<SmallBitSet>>,
}

impl CpuRenderer {
    #[inline]
    pub fn new() -> Self {
        Self::default()
    }

    #[inline]
    pub fn create_buffer_layer_cache(&mut self) -> Option<BufferLayerCache> {
        self.buffers_with_caches
            .borrow_mut()
            .first_empty_slot()
            .map(|id| BufferLayerCache::new(id, Rc::downgrade(&self.buffers_with_caches)))
    }

    pub fn render<L>(
        &mut self,
        composition: &mut Composition,
        buffer: &mut Buffer<'_, '_, L>,
        mut channels: [Channel; 4],
        clear_color: Color,
        crop: Option<Rect>,
    ) where
        L: Layout,
    {
        // If `clear_color` has alpha = 1 we can upgrade the alpha channel to `Channel::One`
        // in order to skip reading the alpha channel.
        if clear_color.a == 1.0 {
            channels = channels.map(|c| match c {
                Channel::Alpha => Channel::One,
                c => c,
            });
        }

        if let Some(layer_cache) = buffer.layer_cache.as_ref() {
            let tiles_len = buffer.layout.width_in_tiles() * buffer.layout.height_in_tiles();
            let cache = &layer_cache.cache;

            cache.borrow_mut().layers.resize(tiles_len, None);

            if cache.borrow().width != Some(buffer.layout.width())
                || cache.borrow().height != Some(buffer.layout.height())
            {
                cache.borrow_mut().width = Some(buffer.layout.width());
                cache.borrow_mut().height = Some(buffer.layout.height());

                layer_cache.clear();
            }
        }

        composition.compact_geom();
        composition.shared_state.borrow_mut().props_interner.compact();

        let layers = &composition.layers;
        let shared_state = &mut *composition.shared_state.borrow_mut();
        let lines_builder = &mut shared_state.lines_builder;
        let geom_id_to_order = &shared_state.geom_id_to_order;
        let rasterizer = &mut self.rasterizer;

        struct CompositionContext<'l> {
            layers: &'l FxHashMap<Order, Layer>,
            cache_id: Option<u8>,
        }

        impl LayerProps for CompositionContext<'_> {
            #[inline]
            fn get(&self, id: u32) -> Cow<'_, Props> {
                Cow::Borrowed(
                    self.layers
                        .get(&Order::new(id).expect("PixelSegment layer_id cannot overflow Order"))
                        .map(|layer| &layer.props)
                        .expect(
                            "Layers outside of HashMap should not produce visible PixelSegments",
                        ),
                )
            }

            #[inline]
            fn is_unchanged(&self, id: u32) -> bool {
                match self.cache_id {
                    None => false,
                    Some(cache_id) => self
                        .layers
                        .get(&Order::new(id).expect("PixelSegment layer_id cannot overflow Order"))
                        .map(|layer| layer.is_unchanged(cache_id))
                        .expect(
                            "Layers outside of HashMap should not produce visible PixelSegments",
                        ),
                }
            }
        }

        let context = CompositionContext {
            layers,
            cache_id: buffer.layer_cache.as_ref().map(|cache| cache.id),
        };

        // `take()` sets the RefCell's content with `Default::default()` which is cheap for Option.
        let builder = lines_builder.take().expect("lines_builder should not be None");

        *lines_builder = {
            let lines = {
                duration!("gfx", "LinesBuilder::build");
                builder.build(|id| {
                    geom_id_to_order
                        .get(&id)
                        .copied()
                        .flatten()
                        .and_then(|order| context.layers.get(&order))
                        .map(|layer| layer.inner.clone())
                })
            };

            {
                duration!("gfx", "Rasterizer::rasterize");
                rasterizer.rasterize(&lines);
            }
            {
                duration!("gfx", "Rasterizer::sort");
                rasterizer.sort();
            }

            let previous_clear_color = buffer
                .layer_cache
                .as_ref()
                .and_then(|layer_cache| layer_cache.cache.borrow().clear_color);

            let layers_per_tile = buffer.layer_cache.as_ref().map(|layer_cache| {
                RefMut::map(layer_cache.cache.borrow_mut(), |cache| &mut cache.layers)
            });

            {
                duration!("gfx", "painter::for_each_row");
                painter::for_each_row(
                    buffer.layout,
                    buffer.buffer,
                    channels,
                    buffer.flusher.as_deref(),
                    previous_clear_color,
                    layers_per_tile,
                    rasterizer.segments(),
                    clear_color,
                    &crop,
                    &context,
                );
            }

            Some(lines.unwrap())
        };

        if let Some(buffer_layer_cache) = &buffer.layer_cache {
            buffer_layer_cache.cache.borrow_mut().clear_color = Some(clear_color);

            for layer in composition.layers.values_mut() {
                layer.set_is_unchanged(buffer_layer_cache.id, layer.inner.is_enabled);
            }
        }
    }
}
