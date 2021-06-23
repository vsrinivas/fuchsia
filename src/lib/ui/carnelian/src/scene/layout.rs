// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    scene::{
        group::{GroupId, GroupMemberData},
        scene::{GroupBuilder, SceneBuilder},
    },
    Coord, Point, Rect, Size,
};
use euclid::{point2, size2};
use std::ops::Deref;

/// Arranger is a trait that defines how groups can be arranged.
pub trait Arranger: std::fmt::Debug {
    /// Calculate the size of a group, based on the available size and the sizes
    /// of the members of the group.
    fn calculate_size(
        &self,
        group_size: Size,
        member_sizes: &mut [Size],
        member_data: &[&Option<GroupMemberData>],
    ) -> Size;
    /// Return the group-relative positions of the members of this group, based
    /// on the sizes of the members.
    fn arrange(&self, group_size: Size, member_sizes: &[Size]) -> Vec<Point>;
}

/// Reference to an arranger.
pub type ArrangerPtr = Box<dyn Arranger>;

#[derive(Debug, Clone, Copy)]
/// Alignment as defined by
///s[Flutter](https://api.flutter.dev/flutter/painting/Alignment-class.html).
pub struct Alignment {
    location: Point,
}

impl Alignment {
    /// The top left corner.
    pub fn top_left() -> Self {
        Self { location: point2(-1.0, -1.0) }
    }

    /// The center point along the top edge.
    pub fn top_center() -> Self {
        Self { location: point2(0.0, -1.0) }
    }

    /// The top right corner.
    pub fn top_right() -> Self {
        Self { location: point2(1.0, -1.0) }
    }

    /// The center point along the left edge.
    pub fn center_left() -> Self {
        Self { location: point2(-1.0, 0.0) }
    }

    /// The center point, both horizontally and vertically.
    pub fn center() -> Self {
        Self { location: Point::zero() }
    }

    /// The center point along the right edge.
    pub fn center_right() -> Self {
        Self { location: point2(1.0, 0.0) }
    }

    /// The bottom left corner.
    pub fn bottom_left() -> Self {
        Self { location: point2(-1.0, 1.0) }
    }

    /// The center point along the bottom edge.
    pub fn bottom_center() -> Self {
        Self { location: point2(0.0, 1.0) }
    }

    /// The bottom right corner.
    pub fn bottom_right() -> Self {
        Self { location: point2(1.0, 1.0) }
    }

    fn arrange(&self, facet_size: &Size, group_size: &Size) -> Point {
        let half_delta = (*group_size - *facet_size) / 2.0;

        let x = half_delta.width + self.location.x * half_delta.width;
        let y = half_delta.height + self.location.y * half_delta.height;

        point2(x, y)
    }
}

impl Default for Alignment {
    fn default() -> Self {
        Self::top_left()
    }
}

#[derive(Default, Debug)]
/// Options for a stack arranger.
pub struct StackOptions {
    /// When true, expand the stack to use all the available space.
    pub expand: bool,
    /// How should the stack group members be aligned.
    pub alignment: Alignment,
}

#[derive(Debug)]
/// Stack arranger.
pub struct Stack {
    expand: bool,
    alignment: Alignment,
}

impl Stack {
    /// Create a stack with the provided options.
    pub fn with_options(options: StackOptions) -> Self {
        Self { expand: options.expand, alignment: options.alignment }
    }

    /// Create a stack with the the default options.
    pub fn new() -> Self {
        Self::with_options(StackOptions::default())
    }

    /// Create a boxed stack with the provided options.
    pub fn with_options_ptr(options: StackOptions) -> ArrangerPtr {
        Box::new(Self::with_options(options))
    }

    /// Create a boxed stack with the the default options.
    pub fn new_ptr() -> ArrangerPtr {
        Box::new(Self::new())
    }
}

impl Arranger for Stack {
    fn calculate_size(
        &self,
        group_size: Size,
        member_sizes: &mut [Size],
        _member_data: &[&Option<GroupMemberData>],
    ) -> Size {
        if self.expand {
            group_size
        } else {
            let mut desired_size = Rect::zero();

            for member in member_sizes.iter() {
                let r = Rect::from_size(*member);
                desired_size = desired_size.union(&r);
            }

            desired_size.size
        }
    }

    fn arrange(&self, group_size: Size, member_sizes: &[Size]) -> Vec<Point> {
        member_sizes
            .iter()
            .map(|facet_size| self.alignment.arrange(facet_size, &group_size))
            .collect()
    }
}

/// Fluent builder for stack groups.
pub struct StackBuilder<'a> {
    builder: GroupBuilder<'a>,
    stack_options: StackOptions,
}

impl<'a> StackBuilder<'a> {
    pub(crate) fn new(builder: GroupBuilder<'a>) -> Self {
        Self { builder, stack_options: StackOptions::default() }
    }

    /// Set the expand option for this group.
    pub fn expand(mut self) -> Self {
        self.stack_options.expand = true;
        self
    }

    /// Set the center alignment option for this group.
    pub fn center(mut self) -> Self {
        self.stack_options.alignment = Alignment::center();
        self
    }

    /// Set the alignment option for this group.
    pub fn align(mut self, align: Alignment) -> Self {
        self.stack_options.alignment = align;
        self
    }

    /// Create the stack group, with contents provided by
    /// `f`.
    pub fn contents<F>(mut self, f: F) -> GroupId
    where
        F: FnMut(&mut SceneBuilder),
    {
        self.builder.arranger = Some(Stack::with_options_ptr(self.stack_options));
        self.builder.contents(f)
    }
}

#[derive(Debug)]
/// Direction for Flex.
pub enum Axis {
    /// Left to right.
    Horizontal,
    /// Top to bottom.
    Vertical,
}

impl Axis {
    /// For a size, return the component along the axis.
    pub fn span(&self, size: &Size) -> Coord {
        match self {
            Self::Horizontal => size.width,
            Self::Vertical => size.height,
        }
    }

    /// For a size, return the component across the axis.
    pub fn cross_span(&self, size: &Size) -> Coord {
        match self {
            Self::Horizontal => size.height,
            Self::Vertical => size.width,
        }
    }

    /// For a point, return the component along the axis.
    pub fn position(&self, pos: &Point) -> Coord {
        match self {
            Self::Horizontal => pos.x,
            Self::Vertical => pos.y,
        }
    }

    /// For a point, return the component across the axis.
    pub fn cross_position(&self, pos: &Point) -> Coord {
        match self {
            Self::Horizontal => pos.y,
            Self::Vertical => pos.x,
        }
    }

    /// Create a point based on this direction.
    pub fn point2(&self, axis: Coord, cross_axis: Coord) -> Point {
        match self {
            Self::Horizontal => point2(axis, cross_axis),
            Self::Vertical => point2(cross_axis, axis),
        }
    }

    /// Create a size based on this direction.
    pub fn size2(&self, axis: Coord, cross_axis: Coord) -> Size {
        match self {
            Self::Horizontal => size2(axis, cross_axis),
            Self::Vertical => size2(cross_axis, axis),
        }
    }
}

impl Default for Axis {
    fn default() -> Self {
        Self::Vertical
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
/// Defines how a Flex arranger should arrange group members
/// along its main axis. Based on
/// [Flutters's MainAxisAlignment](https://api.flutter.dev/flutter/rendering/MainAxisAlignment-class.html)
/// but does not currently implement anything related to text direction.
pub enum MainAxisAlignment {
    /// Place the group members as close to the start of the main axis as possible.
    Start,
    /// Place the group members as close to the end of the main axis as possible.
    End,
    /// Place the group members as close to the middle of the main axis as possible.
    Center,
    /// Place the free space evenly between the group members.
    SpaceBetween,
    /// Place the free space evenly between the group members as well as half of that
    /// space before and after the first and last member.
    SpaceAround,
    /// Place the free space evenly between the group members as well as before and
    /// after the first and last member.
    SpaceEvenly,
}

impl Default for MainAxisAlignment {
    fn default() -> Self {
        Self::Start
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
/// Defines how a Flex arranger should arrange group members
/// across its main axis. Based on
/// [Flutters's CrossAxisAlignment](https://api.flutter.dev/flutter/rendering/CrossAxisAlignment-class.html)
/// but does not currently implement baseline or stretch.
pub enum CrossAxisAlignment {
    /// Place the group members with their start edge aligned with the start side of
    /// the cross axis.
    Start,
    /// Place the group members as close to the end of the cross axis as possible.
    End,
    /// Place the group members so that their centers align with the middle of the
    /// cross axis.
    Center,
}

impl Default for CrossAxisAlignment {
    fn default() -> Self {
        Self::Center
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
/// Defines how much space should be occupied in the main axis for a Flex arranger.
/// [Flutters's MainAxisSize](https://api.flutter.dev/flutter/rendering/MainAxisSize-class.html).
pub enum MainAxisSize {
    /// Minimize the amount of free space along the main axis.
    Min,
    /// Maximize the amount of free space along the main axis.
    Max,
}

impl Default for MainAxisSize {
    fn default() -> Self {
        Self::Min
    }
}

#[derive(Default, Debug)]
/// Options for a flex arranger.
pub struct FlexOptions {
    /// The direction to use as the main axis.
    pub direction: Axis,
    /// How much space should be occupied in the main axis.
    pub main_size: MainAxisSize,
    /// How the group members should be placed along the main axis.
    pub main_align: MainAxisAlignment,
    /// How the group members should be placed along the cross axis.
    pub cross_align: CrossAxisAlignment,
}

impl FlexOptions {
    /// Create FlexOptions for a column.
    pub fn column(
        main_size: MainAxisSize,
        main_align: MainAxisAlignment,
        cross_align: CrossAxisAlignment,
    ) -> Self {
        Self { direction: Axis::Vertical, main_size, main_align, cross_align }
    }

    /// Create FlexOptions for a row.
    pub fn row(
        main_size: MainAxisSize,
        main_align: MainAxisAlignment,
        cross_align: CrossAxisAlignment,
    ) -> Self {
        Self { direction: Axis::Horizontal, main_size, main_align, cross_align }
    }
}

#[derive(Clone)]
/// Member data for the Flex arranger.
pub struct FlexMemberData {
    flex: usize,
}

impl FlexMemberData {
    /// Create new member data for a member of a flex group.
    pub fn new(flex: usize) -> Option<GroupMemberData> {
        Some(Box::new(FlexMemberData { flex }))
    }

    pub(crate) fn from(data: &Option<GroupMemberData>) -> Option<FlexMemberData> {
        data.as_ref()
            .and_then(|has_data| has_data.downcast_ref::<FlexMemberData>())
            .map(|has_data| has_data.clone())
    }
}

#[derive(Debug)]
/// Flex group arranger.
pub struct Flex {
    direction: Axis,
    main_size: MainAxisSize,
    main_align: MainAxisAlignment,
    cross_align: CrossAxisAlignment,
}

impl Flex {
    /// Create a flex arranger with the provided options.
    pub fn with_options(options: FlexOptions) -> Self {
        Self {
            direction: options.direction,
            main_size: options.main_size,
            main_align: options.main_align,
            cross_align: options.cross_align,
        }
    }

    /// Create a flex arranger with default options.
    pub fn new() -> Self {
        Self::with_options(FlexOptions::default())
    }

    /// Create a boxed flex arranger with the provided options.
    pub fn with_options_ptr(options: FlexOptions) -> ArrangerPtr {
        Box::new(Self::with_options(options))
    }

    /// Create a boxed flex arranger with default options.
    pub fn new_ptr() -> ArrangerPtr {
        Box::new(Self::new())
    }
}

impl Arranger for Flex {
    fn calculate_size(
        &self,
        group_size: Size,
        member_sizes: &mut [Size],
        member_data: &[&Option<GroupMemberData>],
    ) -> Size {
        let group_main_span = self.direction.span(&group_size);
        let weights: Vec<usize> = member_data
            .iter()
            .map(|member_data| {
                FlexMemberData::from(member_data).and_then(|md| Some(md.flex)).unwrap_or(0)
            })
            .collect();
        let total_size: f32 =
            member_sizes.iter().map(|facet_size| self.direction.span(facet_size)).sum();
        let sum_of_weights: usize = weights.iter().sum();
        let weighed_extra = (group_main_span - total_size) / sum_of_weights as f32;
        let axis_size = if self.main_size == MainAxisSize::Max || sum_of_weights > 0 {
            self.direction.span(&group_size)
        } else {
            member_sizes.iter().map(|s| self.direction.span(s)).sum()
        };
        let cross_axis_size = member_sizes
            .iter()
            .map(|s| self.direction.cross_span(s))
            .max_by(|a, b| a.partial_cmp(b).expect("partial_cmp"))
            .unwrap_or(0.0);
        for (index, weight) in weights.iter().enumerate() {
            if *weight > 0 {
                member_sizes[index] += self.direction.size2(weighed_extra * (*weight as f32), 0.0);
            }
        }
        match self.direction {
            Axis::Vertical => size2(cross_axis_size, axis_size),
            Axis::Horizontal => size2(axis_size, cross_axis_size),
        }
    }

    fn arrange(&self, group_size: Size, member_sizes: &[Size]) -> Vec<Point> {
        let item_count = member_sizes.len();
        let group_main_span = self.direction.span(&group_size);
        let group_cross_span = self.direction.cross_span(&group_size);
        let total_size: f32 =
            member_sizes.iter().map(|facet_size| self.direction.span(facet_size)).sum();
        let free_space = group_main_span - total_size;
        let (mut pos, between) = match self.main_align {
            MainAxisAlignment::Start => (0.0, 0.0),
            MainAxisAlignment::End => (group_main_span - total_size, 0.0),
            MainAxisAlignment::Center => (group_main_span / 2.0 - total_size / 2.0, 0.0),
            MainAxisAlignment::SpaceBetween => (0.0, free_space / (item_count - 1) as Coord),
            MainAxisAlignment::SpaceAround => {
                let padding = free_space / item_count as Coord;
                (padding / 2.0, padding)
            }
            MainAxisAlignment::SpaceEvenly => {
                let padding = free_space / (item_count + 1) as Coord;
                (padding, padding)
            }
        };
        let mut positions = Vec::new();
        for facet_size in member_sizes {
            let cross = match self.cross_align {
                CrossAxisAlignment::Start => 0.0,
                CrossAxisAlignment::End => {
                    group_cross_span - self.direction.cross_span(&facet_size)
                }
                CrossAxisAlignment::Center => {
                    group_cross_span / 2.0 - self.direction.cross_span(&facet_size) / 2.0
                }
            };
            positions.push(self.direction.point2(pos, cross));
            pos += self.direction.span(facet_size);
            pos += between;
        }
        positions
    }
}

/// Fluent builder for flex groups
pub struct FlexBuilder<'a> {
    builder: GroupBuilder<'a>,
    flex_options: FlexOptions,
    member_data: Option<GroupMemberData>,
}

impl<'a> FlexBuilder<'a> {
    pub(crate) fn new(
        builder: GroupBuilder<'a>,
        direction: Axis,
        member_data: Option<GroupMemberData>,
    ) -> Self {
        let flex_options = FlexOptions { direction, ..FlexOptions::default() };
        Self { builder, flex_options, member_data }
    }

    /// Use MainAxisSize::Min for main size.
    pub fn min_size(mut self) -> Self {
        self.flex_options.main_size = MainAxisSize::Min;
        self
    }

    /// Use MainAxisSize::Max for main size.
    pub fn max_size(mut self) -> Self {
        self.flex_options.main_size = MainAxisSize::Max;
        self
    }

    /// Use a specific main size.
    pub fn main_size(mut self, main_size: MainAxisSize) -> Self {
        self.flex_options.main_size = main_size;
        self
    }

    /// Use MainAxisAlignment::SpaceEvenly for main alignment.
    pub fn space_evenly(mut self) -> Self {
        self.flex_options.main_align = MainAxisAlignment::SpaceEvenly;
        self
    }

    /// Use a particular main axis alignnment.
    pub fn main_align(mut self, main_align: MainAxisAlignment) -> Self {
        self.flex_options.main_align = main_align;
        self
    }

    /// Use a particular cross axis alignnment.
    pub fn cross_align(mut self, cross_align: CrossAxisAlignment) -> Self {
        self.flex_options.cross_align = cross_align;
        self
    }

    /// Create the stack group, with contents provided by
    /// `f`.
    pub fn contents<F>(mut self, f: F) -> GroupId
    where
        F: FnMut(&mut SceneBuilder),
    {
        self.builder.arranger = Some(Flex::with_options_ptr(self.flex_options));
        self.builder.contents_with_member_data(self.member_data, f)
    }
}

impl<'a> Deref for FlexBuilder<'a> {
    type Target = GroupBuilder<'a>;

    fn deref(&self) -> &Self::Target {
        &self.builder
    }
}
