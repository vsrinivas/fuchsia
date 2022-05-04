// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use euclid::default::{Transform2D, Vector2D};
use forma::{Color as FormaColor, GeomPresTransform, Order as FormaOrder};
use std::convert::TryFrom;

use crate::{
    color::Color,
    geometry::Coord,
    render::generic::{
        forma::{Forma, FormaRaster},
        BlendMode, Composition, Fill, FillRule, GradientType, Layer, Order,
    },
};

#[derive(Debug)]
pub struct FormaComposition {
    pub(crate) id: Option<usize>,
    pub(crate) composition: forma::Composition,
    cached_display_transform: Option<Transform2D<Coord>>,
    pub(crate) background_color: Color,
}

impl FormaComposition {
    fn insert_in_composition(&mut self, order: FormaOrder, raster: &FormaRaster) {
        let mut option = raster.layer_details.borrow_mut();
        let layer_details = option
            .filter(|&(id, layer_translation)| {
                self.composition.get_order_if_stored(id) == Some(order)
                    && raster.translation == layer_translation
            })
            .unwrap_or_else(|| {
                let layer = self.composition.get_mut_or_insert_default(order);

                layer.clear();

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
                    layer.insert(&print.path.transform(&transform));
                }

                (layer.geom_id(), raster.translation)
            });

        *option = Some(layer_details);
    }

    fn forma_transform(&self, translation: Vector2D<Coord>) -> [f32; 6] {
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

            for (_, layer) in self.composition.layers_mut() {
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

impl Composition<Forma> for FormaComposition {
    fn new(background_color: Color) -> Self {
        Self {
            id: None,
            composition: forma::Composition::new(),
            cached_display_transform: None,
            background_color,
        }
    }

    fn clear(&mut self) {
        for (_, layer) in self.composition.layers_mut() {
            layer.clear();
        }
    }

    fn insert(&mut self, order: Order, layer: Layer<Forma>) {
        let forma_order =
            FormaOrder::try_from(order.as_u32() * 2).unwrap_or_else(|e| panic!("{}", e));
        if let Some(ref clip) = layer.clip {
            self.insert_in_composition(forma_order, clip);

            let forma_transform = self.forma_transform(layer.raster.translation);
            let forma_layer = self.composition.get_mut(forma_order).unwrap();

            forma_layer.set_props(forma::Props {
                fill_rule: match layer.style.fill_rule {
                    FillRule::NonZero => forma::FillRule::NonZero,
                    FillRule::EvenOdd => forma::FillRule::EvenOdd,
                },
                func: forma::Func::Clip(1),
            });

            forma_layer.set_transform(
                GeomPresTransform::try_from(forma_transform).unwrap_or_else(|e| panic!("{}", e)),
            );
        } else {
            self.composition.remove(forma_order);
        }

        let forma_order =
            FormaOrder::try_from(order.as_u32() * 2 + 1).unwrap_or_else(|e| panic!("{}", e));

        if layer.raster.prints.is_empty() {
            self.composition.remove(forma_order);
            return;
        }

        self.insert_in_composition(forma_order, &layer.raster);

        let forma_transform = self.forma_transform(layer.raster.translation);
        let forma_layer = self.composition.get_mut(forma_order).unwrap();

        forma_layer.set_props(forma::Props {
            fill_rule: match layer.style.fill_rule {
                FillRule::NonZero => forma::FillRule::NonZero,
                FillRule::EvenOdd => forma::FillRule::EvenOdd,
            },
            func: forma::Func::Draw(forma::Style {
                fill: match &layer.style.fill {
                    Fill::Solid(color) => forma::Fill::Solid(FormaColor::from(color)),
                    Fill::Gradient(gradient) => {
                        let mut builder = forma::GradientBuilder::new(
                            forma::Point::new(gradient.start.x, gradient.start.y),
                            forma::Point::new(gradient.end.x, gradient.end.y),
                        );
                        builder.r#type(match gradient.r#type {
                            GradientType::Linear => forma::GradientType::Linear,
                            GradientType::Radial => forma::GradientType::Radial,
                        });

                        for &(color, stop) in &gradient.stops {
                            builder.color_with_stop(FormaColor::from(&color), stop);
                        }

                        forma::Fill::Gradient(builder.build().unwrap())
                    }
                },
                is_clipped: layer.clip.is_some(),
                blend_mode: match layer.style.blend_mode {
                    BlendMode::Over => forma::BlendMode::Over,
                    BlendMode::Screen => forma::BlendMode::Screen,
                    BlendMode::Overlay => forma::BlendMode::Overlay,
                    BlendMode::Darken => forma::BlendMode::Darken,
                    BlendMode::Lighten => forma::BlendMode::Lighten,
                    BlendMode::ColorDodge => forma::BlendMode::ColorDodge,
                    BlendMode::ColorBurn => forma::BlendMode::ColorBurn,
                    BlendMode::HardLight => forma::BlendMode::HardLight,
                    BlendMode::SoftLight => forma::BlendMode::SoftLight,
                    BlendMode::Difference => forma::BlendMode::Difference,
                    BlendMode::Exclusion => forma::BlendMode::Exclusion,
                    BlendMode::Multiply => forma::BlendMode::Multiply,
                    BlendMode::Hue => forma::BlendMode::Hue,
                    BlendMode::Saturation => forma::BlendMode::Saturation,
                    BlendMode::Color => forma::BlendMode::Color,
                    BlendMode::Luminosity => forma::BlendMode::Luminosity,
                },
                ..Default::default()
            }),
        });

        forma_layer.set_transform(
            GeomPresTransform::try_from(forma_transform).unwrap_or_else(|e| panic!("{}", e)),
        );
    }

    fn remove(&mut self, order: Order) {
        for i in 0..2 {
            let order =
                FormaOrder::try_from(order.as_u32() * 2 + i).unwrap_or_else(|e| panic!("{}", e));
            self.composition.remove(order);
        }
    }
}
