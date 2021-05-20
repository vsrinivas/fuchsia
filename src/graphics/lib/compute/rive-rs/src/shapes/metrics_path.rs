// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::num::NonZeroUsize;

use crate::{
    math,
    shapes::command_path::{Command, CommandPath, CommandPathBuilder},
};

#[derive(Clone, Copy, Debug)]
struct CubicSegment {
    t: f32,
    len: f32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum PathPartType {
    Line,
    Cubic(NonZeroUsize),
}

#[derive(Clone, Copy, Debug)]
struct PathPart {
    r#type: PathPartType,
    offset: usize,
    num_segments: usize,
}

impl PathPart {
    pub fn line(offset: usize) -> Self {
        Self { r#type: PathPartType::Line, offset, num_segments: 0 }
    }

    pub fn cubic(offset: usize) -> Self {
        Self { r#type: PathPartType::Cubic(NonZeroUsize::new(1).unwrap()), offset, num_segments: 0 }
    }
}

fn compute_hull(
    from: math::Vec,
    from_out: math::Vec,
    to_in: math::Vec,
    to: math::Vec,
    t: f32,
    hull: &mut [math::Vec; 6],
) {
    hull[0] = from.lerp(from_out, t);
    hull[1] = from_out.lerp(to_in, t);
    hull[2] = to_in.lerp(to, t);

    hull[3] = hull[0].lerp(hull[1], t);
    hull[4] = hull[1].lerp(hull[2], t);

    hull[5] = hull[3].lerp(hull[4], t);
}

fn too_far(a: math::Vec, b: math::Vec) -> bool {
    const TOO_FAR: f32 = 1.0;
    (a.x - b.x).abs().max((a.y - b.y).abs()) > TOO_FAR
}

fn should_split_cubic(
    from: math::Vec,
    from_out: math::Vec,
    to_in: math::Vec,
    to: math::Vec,
) -> bool {
    let one_third = from.lerp(to, 1.0 / 3.0);
    let two_thirds = from.lerp(to, 2.0 / 3.0);

    too_far(from_out, one_third) || too_far(to_in, two_thirds)
}

fn segment_cubic(
    from: math::Vec,
    from_out: math::Vec,
    to_in: math::Vec,
    to: math::Vec,
    mut running_length: f32,
    t0: f32,
    t1: f32,
    segments: &mut Vec<CubicSegment>,
) -> f32 {
    const MIN_SEGMENT_LENGTH: f32 = 0.05;

    if should_split_cubic(from, from_out, to_in, to) {
        let half_t = (t0 + t1) / 2.0;

        let mut hull = [math::Vec::default(); 6];
        compute_hull(from, from_out, to_in, to, 0.5, &mut hull);

        running_length =
            segment_cubic(from, hull[0], hull[3], hull[5], running_length, t0, half_t, segments);
        running_length =
            segment_cubic(hull[5], hull[4], hull[2], to, running_length, half_t, t1, segments);
    } else {
        let length = from.distance(to);
        running_length += length;

        if length > MIN_SEGMENT_LENGTH {
            segments.push(CubicSegment { t: t1, len: running_length });
        }
    }

    running_length
}

#[derive(Debug)]
pub struct MetricsPath {
    points: Vec<math::Vec>,
    cubic_segments: Vec<CubicSegment>,
    parts: Vec<PathPart>,
    lengths: Vec<f32>,
}

impl MetricsPath {
    pub fn new(command_path: &CommandPath) -> Self {
        let mut points = Vec::new();
        let mut parts = Vec::new();

        for command in &command_path.commands {
            match *command {
                Command::MoveTo(p) => points.push(p),
                Command::LineTo(p) => {
                    parts.push(PathPart::line(points.len()));
                    points.push(p);
                }
                Command::CubicTo(c0, c1, p) => {
                    parts.push(PathPart::cubic(points.len()));
                    points.push(c0);
                    points.push(c1);
                    points.push(p);
                }
                Command::Close => {
                    if parts.last().map(|part| part.r#type) == Some(PathPartType::Line) {
                        // We auto close the last part if it's a cubic, if it's not then make
                        // sure to add the final part in so we can compute its length too.
                        parts.push(PathPart::line(points.len()));
                        points.push(points[0]);
                    }
                }
            }
        }

        Self { points, cubic_segments: Vec::new(), parts, lengths: Vec::new() }
    }

    pub fn compute_length(&mut self) -> f32 {
        self.cubic_segments.clear();
        self.lengths.clear();

        let mut i = 0;
        let mut length = 0.0;

        for part in &mut self.parts {
            match part.r#type {
                PathPartType::Line => {
                    let from = self.points[i];
                    let to = self.points[i + 1];

                    i += 1;

                    let part_length = from.distance(to);
                    self.lengths.push(part_length);
                    length += part_length;
                }
                PathPartType::Cubic(ref mut ci) => {
                    let from = self.points[i];
                    let from_out = self.points[i + 1];
                    let to_in = self.points[i + 2];
                    let to = self.points[i + 3];

                    i += 3;

                    let index = self.cubic_segments.len();
                    *ci = NonZeroUsize::new(index + 1).unwrap();

                    let part_length = segment_cubic(
                        from,
                        from_out,
                        to_in,
                        to,
                        0.0,
                        0.0,
                        1.0,
                        &mut self.cubic_segments,
                    );
                    self.lengths.push(part_length);
                    length += part_length;
                    part.num_segments = self.cubic_segments.len() - index;
                }
            }
        }

        length
    }

    pub fn trimmed(&self, start_len: f32, end_len: f32, move_to: bool) -> CommandPath {
        assert!(end_len >= start_len);

        let mut builder = CommandPathBuilder::new();

        if start_len == end_len || self.parts.is_empty() {
            return builder.build();
        }

        let parts_and_lengths = self.lengths.iter().scan(0.0, |len, &part_len| {
            let old_len = *len;
            *len += part_len;

            Some((old_len, part_len))
        });

        let first_part = parts_and_lengths
            .clone()
            .enumerate()
            .find(|(_, (len, part_len))| len + part_len > start_len)
            .map(|(i, (len, part_len))| (i, (start_len - len) / part_len));

        match first_part {
            None => return builder.build(),
            Some((first_part_index, start_t)) => {
                let (last_part_index, end_t) = parts_and_lengths
                    .enumerate()
                    .skip(first_part_index)
                    .find(|(_, (len, part_len))| len + part_len >= end_len)
                    .map(|(i, (len, part_len))| (i, (end_len - len) / part_len))
                    .unwrap_or_else(|| (self.parts.len() - 1, 1.0));

                let start_t = start_t.clamp(0.0, 1.0);
                let end_t = end_t.clamp(0.0, 1.0);

                if first_part_index == last_part_index {
                    self.extract_sub_part(first_part_index, start_t, end_t, move_to, &mut builder);
                } else {
                    self.extract_sub_part(first_part_index, start_t, 1.0, move_to, &mut builder);

                    for part in &self.parts[first_part_index + 1..last_part_index] {
                        match part.r#type {
                            PathPartType::Line => {
                                builder.line_to(self.points[part.offset]);
                            }
                            PathPartType::Cubic(_) => {
                                builder.cubic_to(
                                    self.points[part.offset],
                                    self.points[part.offset + 1],
                                    self.points[part.offset + 2],
                                );
                            }
                        }
                    }

                    self.extract_sub_part(last_part_index, 0.0, end_t, false, &mut builder);
                }
            }
        }

        builder.build()
    }

    fn extract_sub_part(
        &self,
        i: usize,
        mut start_t: f32,
        mut end_t: f32,
        move_to: bool,
        builder: &mut CommandPathBuilder,
    ) {
        let part = self.parts[i];
        match part.r#type {
            PathPartType::Line => {
                let from = self.points[part.offset - 1];
                let to = self.points[part.offset];

                let dir = to - from;

                if move_to {
                    builder.move_to(from + dir * start_t);
                }
                builder.line_to(from + dir * end_t);
            }
            PathPartType::Cubic(ci) => {
                let starting_segment_index = ci.get() - 1;
                let mut start_end_segment_index = starting_segment_index;
                let ending_segment_index = starting_segment_index + part.num_segments;

                let len = self.lengths[i];
                if start_t != 0.0 {
                    let start_len = start_t * len;
                    for si in starting_segment_index..ending_segment_index {
                        let segment = self.cubic_segments[si];
                        if segment.len >= start_len {
                            if si == starting_segment_index {
                                start_t = segment.t * (start_len / segment.len);
                            } else {
                                let previous_len = self.cubic_segments[si - 1].len;

                                let t = (start_len - previous_len) / (segment.len - previous_len);
                                start_t = math::lerp(self.cubic_segments[si - 1].t, segment.t, t);
                            }

                            // Help out the ending segment finder by setting its
                            // start to where we landed while finding the first
                            // segment, that way it can skip a bunch of work.
                            start_end_segment_index = si;
                            break;
                        }
                    }
                }

                if end_t != 1.0 {
                    let end_len = end_t * len;
                    for si in start_end_segment_index..ending_segment_index {
                        let segment = self.cubic_segments[si];
                        if segment.len >= end_len {
                            if si == starting_segment_index {
                                end_t = segment.t * (end_len / segment.len);
                            } else {
                                let previous_len = self.cubic_segments[si - 1].len;

                                let t = (end_len - previous_len) / (segment.len - previous_len);
                                end_t = math::lerp(self.cubic_segments[si - 1].t, segment.t, t);
                            }

                            break;
                        }
                    }
                }

                let mut hull = [math::Vec::default(); 6];

                let from = self.points[part.offset - 1];
                let from_out = self.points[part.offset];
                let to_in = self.points[part.offset + 1];
                let to = self.points[part.offset + 2];

                if start_t == 0.0 {
                    compute_hull(from, from_out, to_in, to, end_t, &mut hull);

                    if move_to {
                        builder.move_to(from);
                    }
                    builder.cubic_to(hull[0], hull[3], hull[5]);
                } else {
                    // Split at start since it's non 0.
                    compute_hull(from, from_out, to_in, to, start_t, &mut hull);

                    if move_to {
                        // Move to first point on the right side.
                        builder.move_to(hull[5]);
                    }
                    if end_t == 1.0 {
                        // End is 1, so no further split is necessary just cubicTo
                        // the remaining right side.
                        builder.cubic_to(hull[4], hull[2], to);
                    } else {
                        // End is not 1, so split again and cubic to the left side
                        // of the split and remap endT to the new curve range.
                        compute_hull(
                            hull[5],
                            hull[4],
                            hull[2],
                            to,
                            (end_t - start_t) / (1.0 - start_t),
                            &mut hull,
                        );

                        builder.cubic_to(hull[0], hull[3], hull[5]);
                    }
                }
            }
        }
    }
}
