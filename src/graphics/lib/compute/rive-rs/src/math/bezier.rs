// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem;

use smallvec::SmallVec;

use crate::math;

const TOLERANCE: f32 = 0.005;

fn angle(origin: math::Vec, p0: math::Vec, p1: math::Vec) -> f32 {
    let d0 = p0 - origin;
    let d1 = p1 - origin;

    let cross = d0.x * d1.y - d0.y * d1.x;
    let dot = d0.x * d1.x + d0.y * d1.y;

    cross.atan2(dot)
}

fn line_segment_intersection(
    segment0: [math::Vec; 2],
    segment1: [math::Vec; 2],
) -> Option<math::Vec> {
    let d10 = segment0[1] - segment0[0];
    let d32 = segment1[1] - segment1[0];

    let denom = d10.x * d32.y - d32.x * d10.y;
    if denom == 0.0 {
        return None;
    }

    let denom_is_pos = denom > 0.0;

    let d02 = segment0[0] - segment1[0];
    let s_numer = d10.x * d02.y - d10.y * d02.x;
    if (s_numer < 0.0) == denom_is_pos {
        return None;
    }

    let t_numer = d32.x * d02.y - d32.y * d02.x;
    if (t_numer < 0.0) == denom_is_pos {
        return None;
    }

    if (s_numer > denom) == denom_is_pos || (t_numer > denom) == denom_is_pos {
        return None;
    }

    let t = t_numer / denom;
    Some(segment0[0] + d10 * t)
}

fn align(point: math::Vec, line: [math::Vec; 2]) -> math::Vec {
    let angle = -(line[1].y - line[0].y).atan2(line[1].x - line[0].x);

    math::Vec::new(
        (point.x - line[0].x) * angle.cos() - (point.y - line[0].y) * angle.sin(),
        (point.x - line[0].x) * angle.sin() + (point.y - line[0].y) * angle.cos(),
    )
}

fn approx_eq(p0: math::Vec, p1: math::Vec) -> bool {
    (p0.x - p1.x).abs() <= TOLERANCE && (p0.y - p1.y).abs() <= TOLERANCE
}

fn left_different(points: &[math::Vec]) -> [math::Vec; 2] {
    points
        .windows(2)
        .find_map(|window| {
            if let [p0, p1] = *window {
                (!approx_eq(p0, p1)).then(|| [p0, p1])
            } else {
                unreachable!()
            }
        })
        .expect("Bezier cannot be a point")
}

pub fn right_different(points: &[math::Vec]) -> [math::Vec; 2] {
    points
        .windows(2)
        .rev()
        .find_map(|window| {
            if let [p0, p1] = *window {
                (!approx_eq(p0, p1)).then(|| [p0, p1])
            } else {
                unreachable!()
            }
        })
        .expect("Bezier cannot be a point")
}

fn normal(p0: math::Vec, p1: math::Vec, c: f32) -> Option<math::Vec> {
    let d = (p1 - p0) * c;
    if d.x.abs() == 0.0 && d.y.abs() == 0.0 {
        return None;
    }

    let q = (d.x * d.x + d.y * d.y).sqrt();

    Some(math::Vec::new(-d.y / q, d.x / q))
}

fn normal_left(points: &[math::Vec]) -> math::Vec {
    let c = points.len() as f32 - 1.0;
    let [p0, p1] = left_different(points);

    normal(p0, p1, c).expect("Bezier cannot be a point")
}

fn normal_right(points: &[math::Vec]) -> math::Vec {
    let c = points.len() as f32 - 1.0;
    let [p0, p1] = right_different(points);

    normal(p0, p1, c).expect("Bezier cannot be a point")
}

fn derive(points: &[math::Vec]) -> SmallVec<[math::Vec; 3]> {
    let mut derived = SmallVec::new();

    for window in points.windows(2) {
        if let [p0, p1] = *window {
            derived.push((p1 - p0) * (points.len() - 1) as f32);
        }
    }

    derived
}

fn derive_left(points: &[math::Vec]) -> math::Vec {
    let [p0, p1] = left_different(points);
    (p1 - p0) * (points.len() - 1) as f32
}

fn derive_right(points: &[math::Vec]) -> math::Vec {
    let [p0, p1] = right_different(points);
    (p1 - p0) * (points.len() - 1) as f32
}

fn droots(vals: &[f32]) -> SmallVec<[f32; 2]> {
    let mut droots = SmallVec::new();

    match *vals {
        [a, b] => {
            if a != b {
                droots.push(a / (a - b));
            }
        }
        [a, b, c] => {
            let d = a - 2.0 * b + c;
            if d != 0.0 {
                let m1 = -(b * b - a * c).sqrt();
                let m2 = -a + b;

                droots.push(-(m1 + m2) / d);
                droots.push(-(-m1 + m2) / d);
            } else if b != c && d == 0.0 {
                droots.push((2.0 * b - c) / (2.0 * (b - c)));
            }
        }
        _ => (),
    }

    droots
}

fn hull(points: &[math::Vec; 4], t: f32) -> SmallVec<[math::Vec; 10]> {
    let mut hull = SmallVec::new();
    let mut points = SmallVec::from(*points);
    let mut next_points = SmallVec::new();

    hull.extend(points.iter().copied());

    while points.len() > 1 {
        next_points.clear();

        for window in points.windows(2) {
            if let [p0, p1] = *window {
                let point = p0.lerp(p1, t);
                hull.push(point);
                next_points.push(point);
            }
        }

        mem::swap(&mut points, &mut next_points);
    }

    hull
}

fn split(points: &[math::Vec; 4], t0: f32, t1: f32) -> [math::Vec; 4] {
    let hull0 = hull(points, t0);
    let right = [hull0[9], hull0[8], hull0[6], hull0[3]];

    let t1 = (t1 - t0) / (1.0 - t0);
    let hull1 = hull(&right, t1);

    [hull1[0], hull1[4], hull1[7], hull1[9]]
}

fn line_intersection(
    p0: math::Vec,
    p1: math::Vec,
    p2: math::Vec,
    p3: math::Vec,
) -> Option<math::Vec> {
    let nx =
        (p0.x * p1.y - p0.y * p1.x) * (p2.x - p3.x) - (p0.x - p1.x) * (p2.x * p3.y - p2.y * p3.x);
    let ny =
        (p0.x * p1.y - p0.y * p1.x) * (p2.y - p3.y) - (p0.y - p1.y) * (p2.x * p3.y - p2.y * p3.x);
    let d = (p0.x - p1.x) * (p2.y - p3.y) - (p0.y - p1.y) * (p2.x - p3.x);

    if d == 0.0 {
        return None;
    }

    Some(math::Vec::new(nx / d, ny / d))
}

fn scale(points: &[math::Vec; 4], dist: f32) -> Option<[math::Vec; 4]> {
    if dist == 0.0 {
        return Some(*points);
    }

    let normals = [normal_left(points), normal_right(points)];
    let offset = [points[0] + normals[0] * 10.0, points[3] + normals[1] * 10.0];
    let origin = line_intersection(offset[0], points[0], offset[1], points[3])?;

    let mut new_points = [math::Vec::default(); 4];

    new_points[0] = points[0] + normals[0] * dist;
    new_points[3] = points[3] + normals[1] * dist;

    new_points[1] =
        line_intersection(new_points[0], new_points[0] + derive_left(points), origin, points[1])?;
    new_points[2] =
        line_intersection(new_points[3], new_points[3] + derive_right(points), origin, points[2])?;

    Some(new_points)
}

#[derive(Clone, Debug)]
pub enum Bezier {
    Line([math::Vec; 2]),
    Cubic([math::Vec; 4]),
}

impl Bezier {
    fn is_linear(&self) -> bool {
        match self {
            Self::Line(_) => true,
            Self::Cubic(points) => points
                .iter()
                .copied()
                .all(|point| align(point, [points[0], points[3]]).y.abs() < TOLERANCE),
        }
    }

    fn is_simple(&self) -> bool {
        match self {
            Self::Line(_) => true,
            Self::Cubic(points) => {
                let a0 = angle(points[0], points[3], points[1]);
                let a1 = angle(points[0], points[3], points[2]);

                if (a0 > 0.0 && a1 < 0.0 || a0 < 0.0 && a1 > 0.0) && (a1 - a0).abs() >= TOLERANCE {
                    return false;
                }

                let n0 = normal(points[0], points[1], 3.0);
                let n1 = normal(points[2], points[3], 3.0);

                if let (Some(n0), Some(n1)) = (n0, n1) {
                    let s = n0.x * n1.x + n0.y * n1.y;

                    s.clamp(-1.0, 1.0).acos().abs() < std::f32::consts::PI / 3.0
                } else {
                    false
                }
            }
        }
    }

    pub fn points(&self) -> &[math::Vec] {
        match self {
            Self::Line(points) => points,
            Self::Cubic(points) => points,
        }
    }

    fn points_mut(&mut self) -> &mut [math::Vec] {
        match self {
            Self::Line(points) => points,
            Self::Cubic(points) => points,
        }
    }

    pub fn left_different(&self) -> [math::Vec; 2] {
        left_different(self.points())
    }

    pub fn right_different(&self) -> [math::Vec; 2] {
        right_different(self.points())
    }

    pub fn normalize(&self) -> Option<Self> {
        match *self {
            Self::Line([p0, p1]) => {
                if !approx_eq(p0, p1) {
                    Some(self.clone())
                } else {
                    None
                }
            }
            Self::Cubic([p0, p1, p2, p3]) => {
                if approx_eq(p0, p1) && approx_eq(p2, p3) {
                    Self::Line([p0, p3]).normalize()
                } else {
                    Some(self.clone())
                }
            }
        }
    }

    fn as_segment(&self) -> [math::Vec; 2] {
        match self {
            Self::Line(points) => *points,
            Self::Cubic([p0, _, _, p3]) => [*p0, *p3],
        }
    }

    pub fn intersect(&self, other: &Self) -> bool {
        line_segment_intersection(self.as_segment(), other.as_segment()).is_some()
    }

    fn map(&self, mut f: impl FnMut(math::Vec) -> math::Vec) -> Self {
        let mut clone = self.clone();

        for point in clone.points_mut() {
            *point = f(*point);
        }

        clone
    }

    pub fn rev(&self) -> Self {
        match self {
            Self::Line([p0, p1]) => Self::Line([*p1, *p0]),
            Self::Cubic([p0, p1, p2, p3]) => Self::Cubic([*p3, *p2, *p1, *p0]),
        }
    }

    fn extrema(&self) -> SmallVec<[f32; 8]> {
        let mut extrema: SmallVec<[f32; 8]> = SmallVec::new();

        let is_unit = |t: f32| (t.is_finite() && 0.0 <= t && t <= 1.0).then(|| t.abs());

        if let Self::Cubic(points) = self {
            let d1 = derive(points);
            let d2 = derive(&d1);

            let d1x: SmallVec<[_; 3]> = d1.iter().map(|p| p.x).collect();
            let d2x: SmallVec<[_; 2]> = d2.iter().map(|p| p.x).collect();
            let d1y: SmallVec<[_; 3]> = d1.iter().map(|p| p.y).collect();
            let d2y: SmallVec<[_; 2]> = d2.iter().map(|p| p.y).collect();

            extrema.extend(droots(&d1x).into_iter().filter_map(is_unit));
            extrema.extend(droots(&d2x).into_iter().filter_map(is_unit));
            extrema.extend(droots(&d1y).into_iter().filter_map(is_unit));
            extrema.extend(droots(&d2y).into_iter().filter_map(is_unit));
        }

        extrema.sort_by(|&a, b| a.partial_cmp(b).unwrap());
        extrema.dedup();

        extrema
    }

    fn reduce(&self) -> SmallVec<[Self; 16]> {
        const STEP: f32 = 0.01;

        let mut extrema = self.extrema();

        if extrema.first().map(|&e| e <= STEP) != Some(true) {
            extrema.insert(0, 0.0);
        }

        if extrema.last().map(|&e| (e - 1.0).abs() <= STEP) != Some(true) {
            extrema.push(1.0);
        }

        let mut pass0: SmallVec<[_; 8]> = SmallVec::new();
        let mut pass1 = SmallVec::new();

        if let Self::Cubic(points) = self {
            for window in extrema.windows(2) {
                if let [t0, t1] = *window {
                    pass0.push(split(points, t0, t1));
                }
            }

            'outer: for segment_pass0 in pass0 {
                let mut t0 = 0.0;
                let mut t1 = 0.0;

                while t1 <= 1.0 - TOLERANCE {
                    t1 = t0 + STEP;
                    while t1 <= 1.0 + STEP - TOLERANCE {
                        let segment = split(&segment_pass0, t0, t1);
                        let bezier = Self::Cubic(segment);

                        if !bezier.is_simple() {
                            t1 -= STEP;

                            if (t1 - t0).abs() < STEP {
                                if let Some(normalized) = Self::Cubic(segment_pass0).normalize() {
                                    if t0 == 0.0 {
                                        pass1.push(normalized);
                                    }
                                }
                                continue 'outer;
                            }

                            pass1.push(Self::Cubic(split(&segment_pass0, t0, t1)));

                            t0 = t1;
                            break;
                        }

                        t1 += STEP;
                    }
                }

                if t0 < 1.0 {
                    pass1.push(Self::Cubic(split(&segment_pass0, t0, 1.0)));
                }
            }
        }

        if pass1.is_empty() {
            pass1.push(self.clone());
        }

        pass1
    }

    pub fn offset(&self, dist: f32) -> SmallVec<[Self; 16]> {
        if dist.is_sign_negative() {
            return self.rev().offset(-dist);
        }

        let mut curves = SmallVec::new();

        if self.is_linear() {
            let normal = normal_left(self.points());

            curves.push(
                self.map(|point| {
                    math::Vec::new(point.x + dist * normal.x, point.y + dist * normal.y)
                }),
            );

            return curves;
        }
        self.reduce()
            .iter()
            .filter_map(|bezier| {
                bezier
                    .normalize()
                    .map(|bezier| {
                        if bezier.is_linear() {
                            bezier.offset(dist)[0].clone()
                        } else if let Self::Cubic(points) = bezier {
                            if let Some(scaled) = scale(&points, dist) {
                                Self::Cubic(scaled)
                            } else {
                                Self::Line([points[0], points[3]]).offset(dist)[0].clone()
                            }
                        } else {
                            unreachable!()
                        }
                    })
                    .as_ref()
                    .and_then(Bezier::normalize)
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! assert_approx_eq {
        ($a:expr, $b:expr) => {{
            assert!(
                ($a - $b).abs() < TOLERANCE,
                "assertion failed: `(left !== right)` \
                 (left: `{:?}`, right: `{:?}`)",
                $a,
                $b,
            );
        }};
    }

    #[test]
    fn offset_line() {
        let line = Bezier::Line([math::Vec::new(-0.5, -0.5), math::Vec::new(0.5, 0.5)]);

        let pos_offset = line.offset(2.0f32.sqrt() / 2.0);
        assert_eq!(pos_offset.len(), 1);
        assert_eq!(pos_offset[0].points().len(), 2);
        assert_approx_eq!(pos_offset[0].points()[0].x, -1.0);
        assert_approx_eq!(pos_offset[0].points()[0].y, 0.0);
        assert_approx_eq!(pos_offset[0].points()[1].x, 0.0);
        assert_approx_eq!(pos_offset[0].points()[1].y, 1.0);

        let neg_offset = line.offset(-(2.0f32.sqrt()) / 2.0);
        assert_eq!(neg_offset.len(), 1);
        assert_eq!(neg_offset[0].points().len(), 2);
        assert_approx_eq!(neg_offset[0].points()[1].x, 0.0);
        assert_approx_eq!(neg_offset[0].points()[1].y, -1.0);
        assert_approx_eq!(neg_offset[0].points()[0].x, 1.0);
        assert_approx_eq!(neg_offset[0].points()[0].y, 0.0);
    }
}
