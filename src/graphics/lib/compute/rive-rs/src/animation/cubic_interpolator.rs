// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{any::TypeId, cell::RefCell};

use crate::{
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    importers::{ArtboardImporter, ImportStack},
    status_code::StatusCode,
    Artboard,
};

const SPLINE_TABLE_SIZE: usize = 11;
const SAMPLE_STEP_SIZE: f32 = 1.0 / (SPLINE_TABLE_SIZE as f32 - 1.0);

const NEWTON_ITERATIONS: usize = 4;
const NEWTON_MIN_SLOPE: f32 = 0.001;
const SUBDIVISION_PRECISION: f32 = 0.0000001;
const SUBDIVISION_MAX_ITERATIONS: usize = 10;

#[derive(Debug)]
pub struct CubicInterpolator {
    x1: Property<f32>,
    y1: Property<f32>,
    x2: Property<f32>,
    y2: Property<f32>,
    values: RefCell<[f32; SPLINE_TABLE_SIZE]>,
}

impl ObjectRef<'_, CubicInterpolator> {
    pub fn x1(&self) -> f32 {
        self.x1.get()
    }

    pub fn set_x1(&self, x1: f32) {
        self.x1.set(x1);
    }

    pub fn y1(&self) -> f32 {
        self.y1.get()
    }

    pub fn set_y1(&self, y1: f32) {
        self.y1.set(y1);
    }

    pub fn x2(&self) -> f32 {
        self.x2.get()
    }

    pub fn set_x2(&self, x2: f32) {
        self.x2.set(x2);
    }

    pub fn y2(&self) -> f32 {
        self.y2.get()
    }

    pub fn set_y2(&self, y2: f32) {
        self.y2.set(y2);
    }
}

fn calc_bezier(t: f32, c1: f32, c2: f32) -> f32 {
    (((1.0 - 3.0 * c2 + 3.0 * c1) * t + (3.0 * c2 - 6.0 * c1)) * t + (3.0 * c1)) * t
}

fn get_slope(t: f32, c1: f32, c2: f32) -> f32 {
    3.0 * (1.0 - 3.0 * c2 + 3.0 * c1) * t * t + 2.0 * (3.0 * c2 - 6.0 * c1) * t + (3.0 * c1)
}

impl ObjectRef<'_, CubicInterpolator> {
    pub fn t(&self, x: f32) -> f32 {
        let values = self.values.borrow();
        let mut interval_start = 0.0;
        let mut current_sample = 1;
        let last_sample = SPLINE_TABLE_SIZE - 1;

        while current_sample != last_sample && values[current_sample] <= x {
            interval_start += SAMPLE_STEP_SIZE;

            current_sample += 1;
        }

        current_sample -= 1;

        let dist =
            (x - values[current_sample]) / (values[current_sample + 1] - values[current_sample]);
        let mut guess_for_t = interval_start + dist * SAMPLE_STEP_SIZE;

        let x1 = self.x1();
        let x2 = self.x2();

        let initial_slope = get_slope(guess_for_t, x1, x2);
        if initial_slope >= NEWTON_MIN_SLOPE {
            for _ in 0..NEWTON_ITERATIONS {
                let current_slope = get_slope(guess_for_t, x1, x2);
                if current_slope == 0.0 {
                    return guess_for_t;
                }

                let current_x = calc_bezier(guess_for_t, x1, x2) - x;
                guess_for_t -= current_x / current_slope;
            }
        } else if initial_slope != 0.0 {
            let mut ab = interval_start + SAMPLE_STEP_SIZE;
            let mut i = 0;
            let mut current_t;
            let mut current_x;

            loop {
                current_t = interval_start + (ab - interval_start) / 2.0;
                current_x = calc_bezier(current_t, x1, x2) - x;

                if current_x > 0.0 {
                    ab = current_t;
                } else {
                    interval_start = current_t;
                }

                i += 1;

                if current_x.abs() > SUBDIVISION_PRECISION && i < SUBDIVISION_MAX_ITERATIONS {
                    return current_t;
                }
            }
        }

        guess_for_t
    }

    pub fn transform(&self, mix: f32) -> f32 {
        calc_bezier(self.t(mix), self.y1(), self.y2())
    }
}

impl Core for CubicInterpolator {
    properties![(63, x1, set_x1), (64, y1, set_y1), (65, x2, set_x2), (66, y2, set_y2)];
}

impl OnAdded for ObjectRef<'_, CubicInterpolator> {
    on_added!([on_added_clean]);

    fn on_added_dirty(&self, _context: &dyn CoreContext) -> StatusCode {
        for (i, value) in self.values.borrow_mut().iter_mut().enumerate() {
            *value = calc_bezier(i as f32 * SAMPLE_STEP_SIZE, self.x1(), self.x2());
        }

        StatusCode::Ok
    }

    fn import(&self, object: Object, import_stack: &ImportStack) -> StatusCode {
        if let Some(importer) = import_stack.latest::<ArtboardImporter>(TypeId::of::<Artboard>()) {
            importer.push_object(object);
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}

impl Default for CubicInterpolator {
    fn default() -> Self {
        Self {
            x1: Property::new(0.42),
            y1: Property::new(0.0),
            x2: Property::new(0.58),
            y2: Property::new(1.0),
            values: RefCell::default(),
        }
    }
}
