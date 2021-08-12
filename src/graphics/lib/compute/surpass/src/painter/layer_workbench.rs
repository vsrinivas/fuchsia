// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::Cell,
    mem,
    ops::{ControlFlow, RangeInclusive},
};

use rustc_hash::{FxHashMap, FxHashSet};

use crate::{
    painter::{BlendMode, Cover, CoverCarry, Fill, FillRule, Func, LayerProps, Props, Style},
    rasterizer::{self, CompactSegment},
};

pub(crate) trait LayerPainter {
    fn clear_cells(&mut self);
    fn acc_segment(&mut self, segment: CompactSegment);
    fn acc_cover(&mut self, cover: Cover);
    fn clear(&mut self, color: [f32; 4]);
    fn paint_layer(
        &mut self,
        tile_i: usize,
        tile_j: usize,
        layer: u16,
        props: &Props,
        apply_clip: bool,
    ) -> Cover;
}

#[derive(Clone, Copy, Debug)]
#[repr(transparent)]
struct Index(usize);

#[derive(Debug)]
struct MaskedCell<T> {
    val: T,
    mask: Cell<bool>,
}

#[derive(Debug, Default)]
struct MaskedVec<T> {
    vals: Vec<MaskedCell<T>>,
    skipped: Cell<usize>,
}

impl<T> MaskedVec<T> {
    pub fn len(&self) -> usize {
        self.vals.len()
    }

    pub fn iter(&self) -> impl Iterator<Item = &T> {
        self.vals.iter().map(|cell| &cell.val)
    }

    pub fn iter_with_masks(&self) -> impl Iterator<Item = (&T, bool)> {
        self.vals
            .iter()
            .enumerate()
            .map(move |(i, cell)| (&cell.val, i >= self.skipped.get() && cell.mask.get()))
    }

    pub fn iter_masked(&self) -> impl DoubleEndedIterator<Item = (Index, &T)> {
        self.vals
            .iter()
            .enumerate()
            .skip(self.skipped.get())
            .filter_map(|(i, cell)| cell.mask.get().then(|| (Index(i), &cell.val)))
    }

    pub fn clear(&mut self) {
        self.vals.clear();
        self.skipped.set(0);
    }

    pub fn set_mask(&self, i: Index, mask: bool) {
        self.vals[i.0].mask.set(mask);
    }

    pub fn skip_until(&self, i: Index) {
        self.skipped.set(i.0);
    }
}

impl<T: Copy + Ord + PartialEq> MaskedVec<T> {
    pub fn sort_and_dedup(&mut self) {
        self.vals.sort_unstable_by_key(|cell| cell.val);
        self.vals.dedup_by_key(|cell| cell.val);
    }
}

impl<A> Extend<A> for MaskedVec<A> {
    fn extend<T: IntoIterator<Item = A>>(&mut self, iter: T) {
        self.vals.extend(iter.into_iter().map(|val| MaskedCell { val, mask: Cell::new(true) }));
    }
}

#[derive(Debug, PartialEq)]
pub enum TileWriteOp {
    None,
    Solid([f32; 4]),
    ColorBuffer,
}

pub struct Context<'c, P: LayerProps> {
    pub tile_i: usize,
    pub tile_j: usize,
    pub segments: &'c [CompactSegment],
    pub props: &'c P,
    pub previous_layers: Cell<Option<&'c mut Option<u16>>>,
    pub clear_color: [f32; 4],
}

#[derive(Debug, Default)]
pub(crate) struct LayerWorkbench {
    ids: MaskedVec<u16>,
    segment_ranges: FxHashMap<u16, RangeInclusive<usize>>,
    queue_indices: FxHashMap<u16, usize>,
    skip_clipping: FxHashSet<u16>,
    queue: Vec<CoverCarry>,
    next_queue: Vec<CoverCarry>,
}

impl LayerWorkbench {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn init(&mut self, cover_carries: impl IntoIterator<Item = CoverCarry>) {
        self.queue.clear();
        self.queue.extend(cover_carries);
    }

    fn next_tile(&mut self) {
        self.ids.clear();
        self.segment_ranges.clear();
        self.queue_indices.clear();
        self.skip_clipping.clear();

        mem::swap(&mut self.queue, &mut self.next_queue);

        self.next_queue.clear();
    }

    fn segments<'c, P: LayerProps>(
        &self,
        context: &'c Context<'_, P>,
        id: u16,
    ) -> Option<&'c [CompactSegment]> {
        self.segment_ranges.get(&id).map(|range| &context.segments[range.clone()])
    }

    fn cover(&self, id: u16) -> Option<&Cover> {
        self.queue_indices.get(&id).map(|&i| &self.queue[i].cover)
    }

    fn layer_is_full<'c, P: LayerProps>(
        &self,
        context: &'c Context<'_, P>,
        id: u16,
        fill_rule: FillRule,
    ) -> bool {
        self.segments(context, id).is_none()
            && self.cover(id).map(|cover| cover.is_full(fill_rule)).unwrap_or_default()
    }

    fn cover_carry<'c, P: LayerProps>(
        &self,
        context: &'c Context<'_, P>,
        id: u16,
    ) -> Option<CoverCarry> {
        let mut acc_cover = Cover::default();

        if let Some(segments) = self.segments(context, id) {
            for segment in segments {
                acc_cover.as_slice_mut()[segment.tile_y() as usize] += segment.cover();
            }
        }

        if let Some(cover) = self.cover(id) {
            cover.add_cover_to(&mut acc_cover.covers);
        }

        (!acc_cover.is_empty(context.props.get(id).fill_rule))
            .then(|| CoverCarry { cover: acc_cover, layer: id })
    }

    fn tile_unchanged_pass<'c, P: LayerProps>(
        &mut self,
        context: &'c Context<'_, P>,
    ) -> ControlFlow<TileWriteOp> {
        let tile_paint = context.previous_layers.take().and_then(|previous_layers| {
            let layers = self.ids.len() as u16;

            let is_unchanged = if let Some(previous_layers) = previous_layers {
                let old_layers = mem::replace(previous_layers, layers);
                old_layers == layers && self.ids.iter().all(|&id| context.props.is_unchanged(id))
            } else {
                *previous_layers = Some(layers);
                false
            };

            is_unchanged.then(|| TileWriteOp::None)
        });

        match tile_paint {
            Some(tile_paint) => ControlFlow::Break(tile_paint),
            None => ControlFlow::Continue(()),
        }
    }

    fn skip_trivial_clips_pass<'c, P: LayerProps>(
        &mut self,
        context: &'c Context<'_, P>,
    ) -> ControlFlow<TileWriteOp> {
        struct Clip {
            is_full: bool,
            last_layer_id: u16,
            i: Index,
            is_used: bool,
        }

        let mut clip = None;

        for (i, &id) in self.ids.iter_masked() {
            let props = context.props.get(id);

            if let Func::Clip(layers) = props.func {
                let is_full = self.layer_is_full(context, id, props.fill_rule);

                clip = Some(Clip { is_full, last_layer_id: id + layers as u16, i, is_used: false });

                if is_full {
                    // Skip full clips.
                    self.ids.set_mask(i, false);
                }
            }

            if let Func::Draw(Style { is_clipped: true, .. }) = props.func {
                match clip {
                    Some(Clip { is_full, last_layer_id, ref mut is_used, .. })
                        if id <= last_layer_id =>
                    {
                        if is_full {
                            // Skip clipping when clip is full.
                            self.skip_clipping.insert(id);
                        } else {
                            *is_used = true;
                        }
                    }
                    _ => {
                        // Skip layer outside of clip.
                        self.ids.set_mask(i, false);
                    }
                }
            }

            if let Some(Clip { last_layer_id, i, is_used, .. }) = clip {
                if id > last_layer_id {
                    clip = None;

                    if !is_used {
                        // Remove unused clips.
                        self.ids.set_mask(i, false);
                    }
                }
            }
        }

        // Clip layer might be last layer.
        if let Some(Clip { i, is_used, .. }) = clip {
            if !is_used {
                // Remove unused clips.
                self.ids.set_mask(i, false);
            }
        }

        ControlFlow::Continue(())
    }

    fn skip_fully_covered_layers<'c, P: LayerProps>(
        &mut self,
        context: &'c Context<'_, P>,
    ) -> ControlFlow<TileWriteOp> {
        #[derive(Debug)]
        enum InterestingCover {
            Opaque([f32; 4]),
            Incomplete,
        }

        let mut first_interesting_cover = None;

        for (i, &id) in self.ids.iter_masked().rev() {
            let props = context.props.get(id);

            let is_clipped = || {
                matches!(props.func, Func::Draw(Style { is_clipped: true, .. }))
                    && !self.skip_clipping.contains(&id)
            };

            if is_clipped() || !self.layer_is_full(context, id, props.fill_rule) {
                if first_interesting_cover.is_none() {
                    first_interesting_cover = Some(InterestingCover::Incomplete);
                    // The loop does not break here in order to try to cull some layers that are
                    // completely covered.
                }
            } else if let Func::Draw(Style {
                fill: Fill::Solid(color),
                blend_mode: BlendMode::Over,
                ..
            }) = props.func
            {
                if color[3] == 1.0 {
                    if first_interesting_cover.is_none() {
                        first_interesting_cover = Some(InterestingCover::Opaque(color));
                    }

                    self.ids.skip_until(i);

                    break;
                }
            }
        }

        let (i, bottom_color) = match first_interesting_cover {
            // First opaque layer is skipped when blending.
            Some(InterestingCover::Opaque(color)) => (1, color),
            // The clear color is used as a virtual first opqaue layer.
            None => (0, context.clear_color),
            // Visible incomplete cover makes full optimization impossible.
            Some(InterestingCover::Incomplete) => return ControlFlow::Continue(()),
        };

        let color = self.ids.iter_masked().skip(i).try_fold(bottom_color, |dst, (_, &id)| {
            match context.props.get(id).func {
                Func::Draw(Style { fill: Fill::Solid(color), blend_mode, .. }) => {
                    Some(blend_mode.blend(dst, color))
                }
                // Fill is not solid.
                _ => None,
            }
        });

        match color {
            Some(color) => ControlFlow::Break(TileWriteOp::Solid(color)),
            None => ControlFlow::Continue(()),
        }
    }

    fn optimization_passes<'c, P: LayerProps>(
        &mut self,
        context: &'c Context<'_, P>,
    ) -> ControlFlow<TileWriteOp> {
        self.tile_unchanged_pass(context)?;
        self.skip_trivial_clips_pass(context)?;
        self.skip_fully_covered_layers(context)?;

        ControlFlow::Continue(())
    }

    fn populate_layers<'c, P: LayerProps>(&mut self, context: &'c Context<'_, P>) {
        let mut start = 0;
        while let Some(id) = context.segments.get(start).map(|s| s.layer()) {
            let diff =
                rasterizer::search_last_by_key(&context.segments[start..], id, |s| s.layer())
                    .unwrap();

            self.segment_ranges.insert(id, start..=start + diff);

            start += diff + 1;
        }

        self.queue_indices
            .extend(self.queue.iter().enumerate().map(|(i, cover_carry)| (cover_carry.layer, i)));

        self.ids
            .extend(self.segment_ranges.keys().copied().chain(self.queue_indices.keys().copied()));

        self.ids.sort_and_dedup();
    }

    pub fn drive_tile_painting<'c, P: LayerProps>(
        &mut self,
        painter: &mut impl LayerPainter,
        context: &'c Context<'_, P>,
    ) -> TileWriteOp {
        self.populate_layers(context);

        if let ControlFlow::Break(tile_op) = self.optimization_passes(context) {
            for &id in self.ids.iter() {
                if let Some(cover_carry) = self.cover_carry(context, id) {
                    self.next_queue.push(cover_carry);
                }
            }

            self.next_tile();

            return tile_op;
        }

        painter.clear(context.clear_color);

        for (&id, mask) in self.ids.iter_with_masks() {
            if mask {
                painter.clear_cells();

                if let Some(segments) = self.segments(context, id) {
                    for &segment in segments {
                        painter.acc_segment(segment);
                    }
                }

                if let Some(&cover) = self.cover(id) {
                    painter.acc_cover(cover);
                }

                let props = context.props.get(id);
                let mut apply_clip = false;

                if let Func::Draw(Style { is_clipped, .. }) = props.func {
                    apply_clip = is_clipped && !self.skip_clipping.contains(&id);
                }

                let cover =
                    painter.paint_layer(context.tile_i, context.tile_j, id, &props, apply_clip);

                if !cover.is_empty(props.fill_rule) {
                    self.next_queue.push(CoverCarry { cover, layer: id });
                }
            } else if let Some(cover_carry) = self.cover_carry(context, id) {
                self.next_queue.push(cover_carry);
            }
        }

        self.next_tile();

        TileWriteOp::ColorBuffer
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::borrow::Cow;

    use crate::{
        painter::Props,
        simd::{i8x16, Simd},
        PIXEL_WIDTH, TILE_SIZE,
    };

    impl<T: PartialEq, const N: usize> PartialEq<[T; N]> for MaskedVec<T> {
        fn eq(&self, other: &[T; N]) -> bool {
            self.iter_masked().map(|(_, val)| val).eq(other.iter())
        }
    }

    #[test]
    fn masked_vec() {
        let mut v = MaskedVec::default();

        v.extend([1, 2, 3, 4, 5, 6, 7, 8, 9]);

        for (i, &val) in v.iter_masked() {
            if let 2 | 3 | 4 | 5 = val {
                v.set_mask(i, false);
            }

            if val == 3 {
                v.set_mask(i, true);
            }
        }

        assert_eq!(v, [1, 3, 6, 7, 8, 9]);

        for (i, &val) in v.iter_masked() {
            if let 3 | 7 = val {
                v.set_mask(i, false);
            }
        }

        assert_eq!(v, [1, 6, 8, 9]);

        for (i, &val) in v.iter_masked() {
            if val == 8 {
                v.skip_until(i);
            }
        }

        assert_eq!(v, [8, 9]);
    }

    enum CoverType {
        Partial,
        Full,
    }

    fn cover(layer: u16, cover_type: CoverType) -> CoverCarry {
        let cover = match cover_type {
            CoverType::Partial => Cover { covers: [i8x16::splat(1); TILE_SIZE / i8x16::LANES] },
            CoverType::Full => {
                Cover { covers: [i8x16::splat(PIXEL_WIDTH as i8); TILE_SIZE / i8x16::LANES] }
            }
        };

        CoverCarry { cover, layer }
    }

    fn segment(layer: u16) -> CompactSegment {
        CompactSegment::new(0, 0, 0, layer, 0, 0, 0, 0)
    }

    #[test]
    fn populate_layers() {
        let mut workbench = LayerWorkbench::default();

        struct UnimplementedProps;

        impl LayerProps for UnimplementedProps {
            fn get(&self, _layer: u16) -> Cow<'_, Props> {
                unimplemented!()
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        workbench.init([
            cover(0, CoverType::Partial),
            cover(3, CoverType::Partial),
            cover(4, CoverType::Partial),
        ]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[
                segment(0),
                segment(1),
                segment(1),
                segment(2),
                segment(5),
                segment(5),
                segment(5),
            ],
            props: &UnimplementedProps,
            previous_layers: Cell::default(),
            clear_color: [0.0; 4],
        };

        workbench.populate_layers(&context);

        assert_eq!(workbench.ids, [0, 1, 2, 3, 4, 5]);

        assert_eq!(workbench.segment_ranges.len(), 4);
        assert_eq!(workbench.segment_ranges.get(&0).cloned(), Some(0..=0));
        assert_eq!(workbench.segment_ranges.get(&1).cloned(), Some(1..=2));
        assert_eq!(workbench.segment_ranges.get(&2).cloned(), Some(3..=3));
        assert_eq!(workbench.segment_ranges.get(&3).cloned(), None);
        assert_eq!(workbench.segment_ranges.get(&4).cloned(), None);
        assert_eq!(workbench.segment_ranges.get(&5).cloned(), Some(4..=6));

        assert_eq!(workbench.queue_indices.len(), 3);
        assert_eq!(workbench.queue_indices.get(&0).cloned(), Some(0));
        assert_eq!(workbench.queue_indices.get(&1).cloned(), None);
        assert_eq!(workbench.queue_indices.get(&2).cloned(), None);
        assert_eq!(workbench.queue_indices.get(&3).cloned(), Some(1));
        assert_eq!(workbench.queue_indices.get(&4).cloned(), Some(2));
        assert_eq!(workbench.queue_indices.get(&5).cloned(), None);
    }

    #[test]
    fn skip_unchanged() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, _layer: u16) -> Cow<'_, Props> {
                unimplemented!()
            }

            fn is_unchanged(&self, layer: u16) -> bool {
                layer < 5
            }
        }

        let mut layers = Some(4);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[segment(0), segment(1), segment(2), segment(3), segment(4)],
            props: &TestProps,
            previous_layers: Cell::new(Some(&mut layers)),
            clear_color: [0.0; 4],
        };

        workbench.populate_layers(&context);

        // Optimization should fail because the number of layers changed.
        assert_eq!(workbench.tile_unchanged_pass(&context), ControlFlow::Continue(()));
        assert_eq!(layers, Some(5));

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[segment(0), segment(1), segment(2), segment(3), segment(4)],
            props: &TestProps,
            previous_layers: Cell::new(Some(&mut layers)),
            clear_color: [0.0; 4],
        };

        // Skip should occur because the previous pass updated the number of layers.
        assert_eq!(workbench.tile_unchanged_pass(&context), ControlFlow::Break(TileWriteOp::None));
        assert_eq!(layers, Some(5));

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[segment(1), segment(2), segment(3), segment(4), segment(5)],
            props: &TestProps,
            previous_layers: Cell::new(Some(&mut layers)),
            clear_color: [0.0; 4],
        };

        workbench.next_tile();
        workbench.populate_layers(&context);

        // Optimization should fail because at least one layer changed.
        assert_eq!(workbench.tile_unchanged_pass(&context), ControlFlow::Continue(()));
        assert_eq!(layers, Some(5));
    }

    #[test]
    fn skip_full_clip() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, layer: u16) -> Cow<'_, Props> {
                Cow::Owned(match layer {
                    1 | 3 => Props { func: Func::Clip(1), ..Default::default() },
                    _ => Props {
                        func: Func::Draw(Style { is_clipped: layer == 2, ..Default::default() }),
                        ..Default::default()
                    },
                })
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        workbench.init([
            cover(0, CoverType::Partial),
            cover(1, CoverType::Full),
            cover(2, CoverType::Partial),
            cover(3, CoverType::Full),
        ]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[],
            props: &TestProps,
            previous_layers: Cell::default(),
            clear_color: [0.0; 4],
        };

        workbench.populate_layers(&context);

        workbench.skip_trivial_clips_pass(&context);

        assert_eq!(workbench.ids, [0, 2]);
        assert!(!workbench.skip_clipping.contains(&0));
        assert!(workbench.skip_clipping.contains(&2));
    }

    #[test]
    fn skip_layer_outside_of_clip() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, _layer: u16) -> Cow<'_, Props> {
                Cow::Owned(Props {
                    func: Func::Draw(Style { is_clipped: true, ..Default::default() }),
                    ..Default::default()
                })
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        workbench.init([cover(0, CoverType::Partial), cover(1, CoverType::Partial)]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[],
            props: &TestProps,
            previous_layers: Cell::default(),
            clear_color: [0.0; 4],
        };

        workbench.populate_layers(&context);

        workbench.skip_trivial_clips_pass(&context);

        assert_eq!(workbench.ids, []);
    }

    #[test]
    fn skip_without_layer_usage() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, layer: u16) -> Cow<'_, Props> {
                Cow::Owned(match layer {
                    1 | 4 => Props { func: Func::Clip(1), ..Default::default() },
                    _ => Props::default(),
                })
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        workbench.init([
            cover(0, CoverType::Partial),
            cover(1, CoverType::Partial),
            cover(3, CoverType::Partial),
            cover(4, CoverType::Partial),
        ]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[],
            props: &TestProps,
            previous_layers: Cell::default(),
            clear_color: [0.0; 4],
        };

        workbench.populate_layers(&context);

        workbench.skip_trivial_clips_pass(&context);

        assert_eq!(workbench.ids, [0, 3]);
    }

    #[test]
    fn skip_everything_below_opaque() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, _layer: u16) -> Cow<'_, Props> {
                Cow::Owned(Props::default())
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        workbench.init([
            cover(0, CoverType::Partial),
            cover(1, CoverType::Partial),
            cover(2, CoverType::Full),
        ]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[segment(3)],
            props: &TestProps,
            previous_layers: Cell::default(),
            clear_color: [0.0; 4],
        };

        workbench.populate_layers(&context);

        assert_eq!(workbench.skip_fully_covered_layers(&context), ControlFlow::Continue(()));

        assert_eq!(workbench.ids, [2, 3]);
    }

    #[test]
    fn blend_top_full_layers() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, layer: u16) -> Cow<'_, Props> {
                Cow::Owned(Props {
                    func: Func::Draw(Style {
                        fill: Fill::Solid([0.5; 4]),
                        blend_mode: match layer {
                            0 => BlendMode::Over,
                            1 => BlendMode::Multiply,
                            _ => unimplemented!(),
                        },
                        ..Default::default()
                    }),
                    ..Default::default()
                })
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        workbench.init([cover(0, CoverType::Full), cover(1, CoverType::Full)]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[],
            props: &TestProps,
            previous_layers: Cell::default(),
            clear_color: [0.0; 4],
        };

        workbench.populate_layers(&context);

        assert_eq!(
            workbench.skip_fully_covered_layers(&context),
            ControlFlow::Break(TileWriteOp::Solid([0.1875, 0.1875, 0.1875, 0.75]))
        );
    }

    #[test]
    fn blend_top_full_layers_with_clear_color() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, _layer: u16) -> Cow<'_, Props> {
                Cow::Owned(Props {
                    func: Func::Draw(Style {
                        fill: Fill::Solid([0.5; 4]),
                        blend_mode: BlendMode::Multiply,
                        ..Default::default()
                    }),
                    ..Default::default()
                })
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        workbench.init([cover(0, CoverType::Full), cover(1, CoverType::Full)]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[],
            props: &TestProps,
            previous_layers: Cell::default(),
            clear_color: [1.0; 4],
        };

        workbench.populate_layers(&context);

        assert_eq!(
            workbench.skip_fully_covered_layers(&context),
            ControlFlow::Break(TileWriteOp::Solid([0.5625, 0.5625, 0.5625, 1.0]))
        );
    }

    #[test]
    fn skip_fully_covered_layers_clip() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, layer: u16) -> Cow<'_, Props> {
                Cow::Owned(Props {
                    func: match layer {
                        0 => Func::Clip(1),
                        1 => Func::Draw(Style {
                            blend_mode: BlendMode::Multiply,
                            ..Default::default()
                        }),
                        _ => unimplemented!(),
                    },
                    ..Default::default()
                })
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        workbench.init([cover(0, CoverType::Partial), cover(1, CoverType::Full)]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[],
            props: &TestProps,
            previous_layers: Cell::default(),
            clear_color: [1.0; 4],
        };

        workbench.populate_layers(&context);

        assert_eq!(workbench.skip_fully_covered_layers(&context), ControlFlow::Continue(()));
    }

    #[test]
    fn skip_clip_then_blend() {
        let mut workbench = LayerWorkbench::default();

        struct TestProps;

        impl LayerProps for TestProps {
            fn get(&self, layer: u16) -> Cow<'_, Props> {
                Cow::Owned(Props {
                    func: match layer {
                        0 => Func::Clip(1),
                        1 => Func::Draw(Style {
                            fill: Fill::Solid([0.5; 4]),
                            blend_mode: BlendMode::Multiply,
                            ..Default::default()
                        }),
                        _ => unimplemented!(),
                    },
                    ..Default::default()
                })
            }

            fn is_unchanged(&self, _layer: u16) -> bool {
                unimplemented!()
            }
        }

        struct UnimplementedPainter;

        impl LayerPainter for UnimplementedPainter {
            fn clear_cells(&mut self) {
                unimplemented!();
            }

            fn acc_segment(&mut self, _segment: CompactSegment) {
                unimplemented!();
            }

            fn acc_cover(&mut self, _cover: Cover) {
                unimplemented!();
            }

            fn clear(&mut self, _color: [f32; 4]) {
                unimplemented!();
            }

            fn paint_layer(
                &mut self,
                _tile_i: usize,
                _tile_j: usize,
                _layer: u16,
                _props: &Props,
                _apply_clip: bool,
            ) -> Cover {
                unimplemented!()
            }
        }

        workbench.init([cover(0, CoverType::Partial), cover(1, CoverType::Full)]);

        let context = Context {
            tile_i: 0,
            tile_j: 0,
            segments: &[],
            props: &TestProps,
            previous_layers: Cell::default(),
            clear_color: [1.0; 4],
        };

        assert_eq!(
            workbench.drive_tile_painting(&mut UnimplementedPainter, &context),
            TileWriteOp::Solid([0.75, 0.75, 0.75, 1.0])
        );
    }
}
