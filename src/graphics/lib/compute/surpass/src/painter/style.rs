// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use crate::simd::f32x8;

#[derive(Clone, Copy, Debug)]
pub enum FillRule {
    NonZero,
    EvenOdd,
}

impl Default for FillRule {
    fn default() -> Self {
        Self::NonZero
    }
}

#[derive(Clone, Copy, Debug)]
pub enum GradientType {
    Linear,
    Radial,
}

const NO_STOP: f32 = -0.0;

#[derive(Clone, Debug)]
pub struct GradientBuilder {
    r#type: GradientType,
    start: [f32; 2],
    end: [f32; 2],
    stops: Vec<([f32; 4], f32)>,
}

impl GradientBuilder {
    pub fn new(start: [f32; 2], end: [f32; 2]) -> Self {
        Self { r#type: GradientType::Linear, start, end, stops: Vec::new() }
    }

    pub fn r#type(&mut self, r#type: GradientType) -> &mut Self {
        self.r#type = r#type;
        self
    }

    pub fn color(&mut self, color: [f32; 4]) -> &mut Self {
        self.stops.push((color, NO_STOP));
        self
    }

    pub fn color_with_stop(&mut self, color: [f32; 4], stop: f32) -> &mut Self {
        if stop < 0.0 || 1.0 < stop {
            panic!("gradient stops must be between 0.0 and 1.0");
        }

        self.stops.push((color, stop));
        self
    }

    pub fn build(mut self) -> Option<Gradient> {
        if self.stops.len() < 2 {
            return None;
        }

        let stop_increment = 1.0 / (self.stops.len() - 1) as f32;
        for (i, (_, stop)) in self.stops.iter_mut().enumerate() {
            if *stop == NO_STOP {
                *stop = i as f32 * stop_increment;
            }
        }

        Some(Gradient {
            r#type: self.r#type,
            start: self.start,
            end: self.end,
            stops: self.stops.into(),
        })
    }
}

#[derive(Clone, Debug)]
pub struct Gradient {
    r#type: GradientType,
    start: [f32; 2],
    end: [f32; 2],
    stops: Arc<[([f32; 4], f32)]>,
}

impl Gradient {
    fn get_t(&self, x: f32, y: f32) -> f32x8 {
        let dx = self.end[0] - self.start[0];
        let dy = self.end[1] - self.start[1];

        let dot = dx * dx + dy * dy;
        let dot_recip = dot.recip();

        match self.r#type {
            GradientType::Linear => {
                let tx = (x - self.start[0]) * dx * dot_recip;
                let ty = y - self.start[1];

                ((f32x8::indexed() + f32x8::splat(ty)) * f32x8::splat(dy))
                    .mul_add(f32x8::splat(dot_recip), f32x8::splat(tx))
            }
            GradientType::Radial => {
                let px = x - self.start[0];
                let px2 = f32x8::splat(px * px);
                let py = f32x8::indexed() + f32x8::splat(y - self.start[1]);

                (py.mul_add(py, px2) * f32x8::splat(dot_recip)).sqrt()
            }
        }
    }

    pub(crate) fn color_at(&self, x: f32, y: f32) -> [f32x8; 4] {
        let mut channels = [f32x8::splat(0.0); 4];

        let t = self.get_t(x, y);

        let mask = t.le(f32x8::splat(self.stops[0].1));
        if mask.any() {
            for (channel, &stop_channel) in channels.iter_mut().zip(self.stops[0].0.iter()) {
                *channel |= f32x8::splat(stop_channel).select(f32x8::splat(0.0), mask);
            }
        }

        let mut start_stop = 0.0;
        let mut start_color = self.stops[0].0;
        let mut acc_mask = mask;

        for &(color, end_stop) in self.stops.iter().skip(1) {
            let mask = acc_mask ^ t.lt(f32x8::splat(end_stop));
            if mask.any() {
                let d = end_stop - start_stop;
                let local_t = (t - f32x8::splat(start_stop)) * f32x8::splat(d.recip());

                for (channel, (&start_channel, &end_channel)) in
                    channels.iter_mut().zip(start_color.iter().zip(color.iter()))
                {
                    *channel |= local_t
                        .mul_add(
                            f32x8::splat(end_channel),
                            (-local_t)
                                .mul_add(f32x8::splat(start_channel), f32x8::splat(start_channel)),
                        )
                        .select(f32x8::splat(0.0), mask);
                }

                acc_mask |= mask;
            }

            start_stop = end_stop;
            start_color = color;
        }

        let mask = !acc_mask;
        if mask.any() {
            for (channel, &stop_channel) in
                channels.iter_mut().zip(self.stops[self.stops.len() - 1].0.iter())
            {
                *channel |= f32x8::splat(stop_channel).select(f32x8::splat(0.0), mask);
            }
        }

        channels
    }
}

#[derive(Clone, Debug)]
pub enum Fill {
    Solid([f32; 4]),
    Gradient(Gradient),
}

impl Default for Fill {
    fn default() -> Self {
        Self::Solid([0.0, 0.0, 0.0, 1.0])
    }
}

#[derive(Clone, Copy, Debug)]
pub enum BlendMode {
    Over,
}

impl Default for BlendMode {
    fn default() -> Self {
        Self::Over
    }
}

#[derive(Clone, Debug, Default)]
pub struct Style {
    pub fill_rule: FillRule,
    pub fill: Fill,
    pub blend_mode: BlendMode,
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! color {
        ( $val:expr ) => {
            [$val, $val, $val, $val]
        };
    }

    macro_rules! color_eq {
        ( $val:expr ) => {{
            assert_eq!($val[0], $val[1]);
            assert_eq!($val[1], $val[2]);
            assert_eq!($val[2], $val[3]);

            $val[0]
        }};
    }

    fn colors(separate: &[f32x8; 4]) -> [[f32; 4]; 8] {
        let mut colors = [[0.0, 0.0, 0.0, 0.0]; 8];

        for i in 0..8 {
            colors[i] = [
                separate[0].as_array()[i],
                separate[1].as_array()[i],
                separate[2].as_array()[i],
                separate[3].as_array()[i],
            ];
        }

        colors
    }

    #[test]
    fn linear_gradient() {
        let mut builder = GradientBuilder::new([0.0, 7.0], [7.0, 0.0]);

        builder
            .color(color!(0.25))
            .color(color!(0.75))
            .color(color!(0.25))
            .color(color!(0.75))
            .color(color!(0.25));

        let gradient = builder.build().unwrap();

        let col = colors(&gradient.color_at(0.0, 0.0));
        assert_eq!(col[0], color!(0.25));
        assert!(color_eq!(col[1]) < color_eq!(col[2]));
        assert!(color_eq!(col[2]) < color_eq!(col[3]));
        assert!(color_eq!(col[4]) > color_eq!(col[5]));
        assert!(color_eq!(col[5]) > color_eq!(col[6]));
        assert_eq!(col[7], color!(0.25));

        let col = colors(&gradient.color_at(3.0, 0.0));
        assert!(color_eq!(col[0]) < 0.75);
        assert!(color_eq!(col[1]) > color_eq!(col[2]));
        assert!(color_eq!(col[2]) > color_eq!(col[3]));
        assert_eq!(col[3], color!(0.25));
        assert!(color_eq!(col[3]) < color_eq!(col[4]));
        assert!(color_eq!(col[4]) < color_eq!(col[5]));
        assert!(color_eq!(col[5]) < color_eq!(col[6]));
        assert!(color_eq!(col[7]) < 0.75);

        let col = colors(&gradient.color_at(7.0, 0.0));
        assert_eq!(col[0], color!(0.25));
        assert!(color_eq!(col[1]) < color_eq!(col[2]));
        assert!(color_eq!(col[2]) < color_eq!(col[3]));
        assert!(color_eq!(col[4]) > color_eq!(col[5]));
        assert!(color_eq!(col[5]) > color_eq!(col[6]));
        assert_eq!(col[7], color!(0.25));
    }

    #[test]
    fn radial_gradient() {
        let mut builder = GradientBuilder::new(
            [0.0, 0.0],
            [7.0 * (1.0 / 2.0f32.sqrt()), 7.0 * (1.0 / 2.0f32.sqrt())],
        );

        builder.r#type(GradientType::Radial).color(color!(0.25)).color(color!(0.75));

        let gradient = builder.build().unwrap();

        let col = colors(&gradient.color_at(0.0, 0.0));
        assert_eq!(col[0], color!(0.25));
        assert!(color_eq!(col[1]) < color_eq!(col[2]));
        assert!(color_eq!(col[2]) < color_eq!(col[3]));
        assert!(color_eq!(col[3]) < color_eq!(col[4]));
        assert!(color_eq!(col[4]) < color_eq!(col[5]));
        assert!(color_eq!(col[5]) < color_eq!(col[6]));
        assert_eq!(col[7], color!(0.75));

        let col = colors(&gradient.color_at(3.0, 0.0));
        assert!(color_eq!(col[0]) < color_eq!(col[1]));
        assert!(color_eq!(col[1]) < color_eq!(col[2]));
        assert!(color_eq!(col[2]) < color_eq!(col[3]));
        assert!(color_eq!(col[3]) < color_eq!(col[4]));
        assert!(color_eq!(col[4]) < color_eq!(col[5]));
        assert!(color_eq!(col[5]) < color_eq!(col[6]));
        assert_eq!(col[7], color!(0.75));

        let col = colors(&gradient.color_at(4.0, 0.0));
        assert!(color_eq!(col[0]) < color_eq!(col[1]));
        assert!(color_eq!(col[1]) < color_eq!(col[2]));
        assert!(color_eq!(col[2]) < color_eq!(col[3]));
        assert!(color_eq!(col[3]) < color_eq!(col[4]));
        assert!(color_eq!(col[4]) < color_eq!(col[5]));
        assert_eq!(col[6], color!(0.75));
        assert_eq!(col[7], color!(0.75));

        let col = colors(&gradient.color_at(7.0, 0.0));
        assert_eq!(col[0], color!(0.75));
        assert_eq!(col[1], color!(0.75));
        assert_eq!(col[2], color!(0.75));
        assert_eq!(col[3], color!(0.75));
        assert_eq!(col[4], color!(0.75));
        assert_eq!(col[5], color!(0.75));
        assert_eq!(col[6], color!(0.75));
        assert_eq!(col[7], color!(0.75));
    }
}
