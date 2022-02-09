// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use euclid::default::{Transform2D, Vector2D};
use mold::{Color as MoldColor, GeomPresTransform, Order as MoldOrder};
use rustc_hash::{FxHashMap, FxHashSet};
use std::convert::TryFrom;

use crate::{
    color::Color,
    geometry::Coord,
    render::generic::{
        mold::{Mold, MoldRaster},
        BlendMode, Composition, Fill, FillRule, GradientType, Layer, Order,
    },
};

#[derive(Debug)]
pub struct MoldComposition {
    pub(crate) id: Option<usize>,
    pub(crate) composition: mold::Composition,
    orders_to_layer_ids: FxHashMap<MoldOrder, mold::LayerId>,
    pub(crate) current_layer_ids: FxHashSet<mold::LayerId>,
    cached_display_transform: Option<Transform2D<Coord>>,
    pub(crate) background_color: Color,
}

impl MoldComposition {
    fn insert_in_composition(&mut self, raster: &MoldRaster) -> mold::LayerId {
        let mut option = raster.layer_details.borrow_mut();
        let layer_details = option
            .filter(|&(layer_id, layer_translation)| {
                self.composition.get(layer_id).is_some() && raster.translation == layer_translation
            })
            .unwrap_or_else(|| {
                let layer_id = self.composition.create_layer();

                for print in &raster.prints {
                    let transform: [f32; 9] = [
                        print.transform.m11,
                        print.transform.m21,
                        print.transform.m31,
                        print.transform.m12,
                        print.transform.m22,
                        print.transform.m32,
                        0.0,
                        0.0,
                        1.0,
                    ];
                    self.composition.insert_in_layer(layer_id, &print.path.transform(&transform));
                }

                (layer_id, raster.translation)
            });

        *option = Some(layer_details);

        layer_details.0
    }

    fn mold_transform(&self, translation: Vector2D<Coord>) -> [f32; 6] {
        if let Some(display_transform) = self.cached_display_transform {
            let transform = display_transform.pre_translate(translation);

            [
                transform.m11,
                transform.m21,
                transform.m12,
                transform.m22,
                transform.m31,
                transform.m32,
            ]
        } else {
            [1.0, 0.0, 0.0, 1.0, translation.x, translation.y]
        }
    }

    pub(crate) fn set_cached_display_transform(&mut self, transform: Transform2D<Coord>) {
        if Some(&transform) != self.cached_display_transform.as_ref() {
            let inverted =
                self.cached_display_transform.and_then(|t| t.inverse()).unwrap_or_default();
            let new_transform = inverted.then(&transform);

            self.cached_display_transform = Some(new_transform);

            for layer in self.composition.layers_mut() {
                let transform = *layer.transform().as_slice();
                let transform = Transform2D {
                    m11: transform[0],
                    m21: transform[1],
                    m12: transform[2],
                    m22: transform[3],
                    m31: transform[4],
                    m32: transform[5],
                    ..Default::default()
                }
                .then(&new_transform);

                layer.set_transform(
                    GeomPresTransform::try_from([
                        transform.m11,
                        transform.m21,
                        transform.m12,
                        transform.m22,
                        transform.m31,
                        transform.m32,
                    ])
                    .unwrap_or_else(|e| panic!("{}", e)),
                );
            }
        }
    }
}

impl Composition<Mold> for MoldComposition {
    fn new(background_color: Color) -> Self {
        Self {
            id: None,
            composition: mold::Composition::new(),
            orders_to_layer_ids: FxHashMap::default(),
            current_layer_ids: FxHashSet::default(),
            cached_display_transform: None,
            background_color,
        }
    }

    fn clear(&mut self) {
        self.current_layer_ids.clear();
        for layer in self.composition.layers_mut() {
            layer.disable();
        }
    }

    fn insert(&mut self, order: Order, layer: Layer<Mold>) {
        for i in 0..2 {
            if let Some(id) = self.orders_to_layer_ids.remove(
                &(MoldOrder::try_from(order.as_u32() * 2 + i)).unwrap_or_else(|e| panic!("{}", e)),
            ) {
                if let Some(layer) = self.composition.get_mut(id) {
                    if !self.current_layer_ids.contains(&id) {
                        layer.disable();
                    }
                }
            }
        }

        if layer.raster.prints.is_empty() {
            return;
        }

        if let Some(ref clip) = layer.clip {
            let id = self.insert_in_composition(clip);

            let mold_transform = self.mold_transform(layer.raster.translation);
            let mold_layer = self.composition.get_mut(id).unwrap();

            let mold_order =
                MoldOrder::try_from(order.as_u32() * 2).unwrap_or_else(|e| panic!("{}", e));
            mold_layer.enable().set_order(mold_order).set_props(mold::Props {
                fill_rule: match layer.style.fill_rule {
                    FillRule::NonZero => mold::FillRule::NonZero,
                    FillRule::EvenOdd => mold::FillRule::EvenOdd,
                },
                func: mold::Func::Clip(1),
            });

            mold_layer.set_transform(
                GeomPresTransform::try_from(mold_transform).unwrap_or_else(|e| panic!("{}", e)),
            );

            self.orders_to_layer_ids.insert(mold_order, id);
            self.current_layer_ids.insert(id);
        }

        let id = self.insert_in_composition(&layer.raster);

        let mold_transform = self.mold_transform(layer.raster.translation);
        let mold_layer = self.composition.get_mut(id).unwrap();
        let mold_order =
            MoldOrder::try_from(order.as_u32() * 2 + 1).unwrap_or_else(|e| panic!("{}", e));
        mold_layer.enable().set_order(mold_order).set_props(mold::Props {
            fill_rule: match layer.style.fill_rule {
                FillRule::NonZero => mold::FillRule::NonZero,
                FillRule::EvenOdd => mold::FillRule::EvenOdd,
            },
            func: mold::Func::Draw(mold::Style {
                fill: match &layer.style.fill {
                    Fill::Solid(color) => mold::Fill::Solid(MoldColor::from(color)),
                    Fill::Gradient(gradient) => {
                        let mut builder = mold::GradientBuilder::new(
                            mold::Point::new(gradient.start.x, gradient.start.y),
                            mold::Point::new(gradient.end.x, gradient.end.y),
                        );
                        builder.r#type(match gradient.r#type {
                            GradientType::Linear => mold::GradientType::Linear,
                            GradientType::Radial => mold::GradientType::Radial,
                        });

                        for &(color, stop) in &gradient.stops {
                            builder.color_with_stop(MoldColor::from(&color), stop);
                        }

                        mold::Fill::Gradient(builder.build().unwrap())
                    }
                },
                is_clipped: layer.clip.is_some(),
                blend_mode: match layer.style.blend_mode {
                    BlendMode::Over => mold::BlendMode::Over,
                    BlendMode::Screen => mold::BlendMode::Screen,
                    BlendMode::Overlay => mold::BlendMode::Overlay,
                    BlendMode::Darken => mold::BlendMode::Darken,
                    BlendMode::Lighten => mold::BlendMode::Lighten,
                    BlendMode::ColorDodge => mold::BlendMode::ColorDodge,
                    BlendMode::ColorBurn => mold::BlendMode::ColorBurn,
                    BlendMode::HardLight => mold::BlendMode::HardLight,
                    BlendMode::SoftLight => mold::BlendMode::SoftLight,
                    BlendMode::Difference => mold::BlendMode::Difference,
                    BlendMode::Exclusion => mold::BlendMode::Exclusion,
                    BlendMode::Multiply => mold::BlendMode::Multiply,
                },
                ..Default::default()
            }),
        });

        mold_layer.set_transform(
            GeomPresTransform::try_from(mold_transform).unwrap_or_else(|e| panic!("{}", e)),
        );

        self.orders_to_layer_ids.insert(mold_order, id);
        self.current_layer_ids.insert(id);
    }

    fn remove(&mut self, order: Order) {
        for i in 0..2 {
            let order =
                MoldOrder::try_from(order.as_u32() * 2 + i).unwrap_or_else(|e| panic!("{}", e));
            if let Some(id) = self.orders_to_layer_ids.remove(&order) {
                if let Some(layer) = self.composition.get_mut(id) {
                    layer.disable();
                }
            }
        }
    }
}
