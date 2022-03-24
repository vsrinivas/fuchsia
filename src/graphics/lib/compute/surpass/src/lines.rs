// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use rayon::prelude::*;

use crate::{extend::ExtendTuple10, Layer, Path, PIXEL_WIDTH};

const MIN_LEN: usize = 1_024;

fn integers_between(a: f32, b: f32) -> u32 {
    let min = a.min(b);
    let max = a.max(b);

    0.max((max.ceil() - min.floor() - 1.0) as u32)
}

fn prefix_sum(vals: &mut [u32]) -> u32 {
    let mut sum = 0;
    for val in vals {
        sum += *val;
        *val = sum;
    }

    sum
}

#[derive(Debug, Default)]
pub struct LinesBuilder {
    lines: Lines,
    cached_len: Cell<usize>,
    cached_until: Cell<usize>,
}

impl LinesBuilder {
    #[inline]
    pub fn new() -> Self {
        Self::default()
    }

    // This type is only used in forma where it does not need `is_empty`.
    #[allow(clippy::len_without_is_empty)]
    #[inline]
    pub fn len(&self) -> usize {
        if self.lines.layer_ids.len() <= self.cached_until.get() {
            self.cached_len.get()
        } else {
            let new_len = self.cached_len.get()
                + self.lines.layer_ids[self.cached_until.get()..]
                    .iter()
                    .filter(|layer_id| layer_id.is_some())
                    .count();

            self.cached_len.set(new_len);
            self.cached_until.set(self.lines.layer_ids.len());

            new_len
        }
    }

    #[inline]
    pub fn push_path(&mut self, layer_id: u32, path: &Path) {
        path.push_lines_to(
            &mut self.lines.x,
            &mut self.lines.y,
            layer_id,
            &mut self.lines.layer_ids,
        );

        self.lines
            .layer_ids
            .resize(self.lines.x.len().checked_sub(1).unwrap_or_default(), Some(layer_id));

        if self.lines.layer_ids.last().map(Option::is_some).unwrap_or_default() {
            self.lines.layer_ids.push(None);
        }
    }

    #[cfg(test)]
    pub fn push(&mut self, layer_id: u32, segment: [crate::Point; 2]) {
        let new_point_needed =
            if let (Some(&x), Some(&y)) = (self.lines.x.last(), self.lines.y.last()) {
                let last_point = crate::Point { x, y };

                last_point != segment[0]
            } else {
                true
            };

        if new_point_needed {
            self.lines.x.push(segment[0].x);
            self.lines.y.push(segment[0].y);
        }

        self.lines.x.push(segment[1].x);
        self.lines.y.push(segment[1].y);

        if self.lines.layer_ids.len() >= 2 {
            match self.lines.layer_ids[self.lines.layer_ids.len() - 2] {
                Some(last_layer_id) if last_layer_id != layer_id => {
                    self.lines.layer_ids.push(Some(layer_id));
                    self.lines.layer_ids.push(None);
                }
                _ => {
                    self.lines.layer_ids.pop();
                    self.lines.layer_ids.push(Some(layer_id));
                    self.lines.layer_ids.push(None);
                }
            }
        } else {
            self.lines.layer_ids.push(Some(layer_id));
            self.lines.layer_ids.push(None);
        }
    }

    pub fn retain<F>(&mut self, mut f: F)
    where
        F: FnMut(u32) -> bool,
    {
        let len = self.lines.x.len();
        let mut del = 0;
        let mut prev_layer_id = None;

        for i in 0..len {
            // `None` layer IDs will always belong to the previous layer ID.
            // Thus, if a layer is removed here, its None will be removed as well.

            let layer_id = self.lines.layer_ids[i];
            let should_retain = layer_id
                .or(prev_layer_id)
                .map(&mut f)
                .expect("consecutive None values should not exist in layer_ids");
            prev_layer_id = layer_id;

            if !should_retain {
                del += 1;
                continue;
            }

            if del > 0 {
                self.lines.x.swap(i - del, i);
                self.lines.y.swap(i - del, i);
                self.lines.layer_ids.swap(i - del, i);
            }
        }

        if del > 0 {
            self.lines.x.truncate(len - del);
            self.lines.y.truncate(len - del);
            self.lines.layer_ids.truncate(len - del);
        }
    }

    #[inline]
    pub fn set_default_transform(&mut self, affine_transform: &[f32; 6]) {
        self.lines.transform = Some(*affine_transform);
    }

    pub fn build<F>(mut self, layers: F) -> Lines
    where
        F: Fn(u32) -> Option<Layer> + Send + Sync,
    {
        let transform = self.lines.transform;
        let ps_layers = self.lines.x.par_windows(2).with_min_len(MIN_LEN).zip_eq(
            self.lines.y.par_windows(2).with_min_len(MIN_LEN).zip_eq(
                self.lines.layer_ids
                    [..self.lines.layer_ids.len().checked_sub(1).unwrap_or_default()]
                    .par_iter()
                    .with_min_len(MIN_LEN),
            ),
        );
        let par_iter = ps_layers.map(|(xs, (ys, &layer_id))| {
            let p0x = xs[0];
            let p0y = ys[0];
            let p1x = xs[1];
            let p1y = ys[1];

            if layer_id.is_none() {
                return Default::default();
            }

            let layer = layer_id.map(&layers).unwrap_or_default();

            if let Some(Layer { is_enabled: false, .. }) = layer {
                return Default::default();
            }

            let order = match layer.as_ref().and_then(|layer| layer.order).or(layer_id) {
                Some(order) => order,
                None => return Default::default(),
            };

            fn transform_point(point: (f32, f32), transform: &[f32; 6]) -> (f32, f32) {
                (
                    transform[0].mul_add(point.0, transform[1].mul_add(point.1, transform[4])),
                    transform[2].mul_add(point.0, transform[3].mul_add(point.1, transform[5])),
                )
            }

            let transform = layer
                .as_ref()
                .and_then(|layer| layer.affine_transform.as_ref())
                .or(transform.as_ref());
            let (p0x, p0y, p1x, p1y) = if let Some(transform) = transform {
                let (p0x, p0y) = transform_point((p0x, p0y), transform);
                let (p1x, p1y) = transform_point((p1x, p1y), transform);

                (p0x, p0y, p1x, p1y)
            } else {
                (p0x, p0y, p1x, p1y)
            };

            if p0y == p1y {
                return Default::default();
            }

            let dx = p1x - p0x;
            let dy = p1y - p0y;
            let dx_recip = dx.recip();
            let dy_recip = dy.recip();

            let t_offset_x = if dx.abs() != 0.0 {
                ((p0x.ceil() - p0x) * dx_recip).max((p0x.floor() - p0x) * dx_recip)
            } else {
                0.0
            };
            let t_offset_y = if dy.abs() != 0.0 {
                ((p0y.ceil() - p0y) * dy_recip).max((p0y.floor() - p0y) * dy_recip)
            } else {
                0.0
            };

            let a = dx_recip.abs();
            let b = dy_recip.abs();
            let c = t_offset_x;
            let d = t_offset_y;

            let length = integers_between(p0x, p1x) + integers_between(p0y, p1y) + 1;

            // Converting to sub-pixel space on th fly by multiplying with `PIXEL_WIDTH`.
            (
                order,
                p0x * PIXEL_WIDTH as f32,
                p0y * PIXEL_WIDTH as f32,
                dx * PIXEL_WIDTH as f32,
                dy * PIXEL_WIDTH as f32,
                a,
                b,
                c,
                d,
                length,
            )
        });

        ExtendTuple10::new((
            &mut self.lines.orders,
            &mut self.lines.x0,
            &mut self.lines.y0,
            &mut self.lines.dx,
            &mut self.lines.dy,
            &mut self.lines.a,
            &mut self.lines.b,
            &mut self.lines.c,
            &mut self.lines.d,
            &mut self.lines.lengths,
        ))
        .par_extend(par_iter);

        prefix_sum(&mut self.lines.lengths);

        self.lines
    }
}

#[derive(Debug, Default)]
pub struct Lines {
    pub x: Vec<f32>,
    pub y: Vec<f32>,
    transform: Option<[f32; 6]>,
    pub layer_ids: Vec<Option<u32>>,
    pub orders: Vec<u32>,
    pub x0: Vec<f32>,
    pub y0: Vec<f32>,
    pub dx: Vec<f32>,
    pub dy: Vec<f32>,
    pub a: Vec<f32>,
    pub b: Vec<f32>,
    pub c: Vec<f32>,
    pub d: Vec<f32>,
    pub lengths: Vec<u32>,
}

impl Lines {
    // This type is only used in forma where it does not need `is_empty`.
    #[allow(clippy::len_without_is_empty)]
    #[inline]
    pub fn len(&self) -> usize {
        self.x.len()
    }

    #[inline]
    pub fn unwrap(mut self) -> LinesBuilder {
        self.orders.clear();
        self.x0.clear();
        self.y0.clear();
        self.dx.clear();
        self.dy.clear();
        self.a.clear();
        self.b.clear();
        self.c.clear();
        self.d.clear();
        self.lengths.clear();

        LinesBuilder { lines: self, ..Default::default() }
    }
}
