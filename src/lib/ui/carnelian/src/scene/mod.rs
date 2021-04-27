// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

use crate::{
    drawing::path_for_corner_knockouts,
    render::{BlendMode, Context as RenderContext, FillRule, Layer, Raster},
    Coord, Point, Rect, Size,
};
use std::{
    collections::BTreeMap,
    sync::atomic::{AtomicUsize, Ordering},
};

/// Individual bits of UI
pub mod facets;
/// Grouping of facets
pub mod group;
/// Layout of facet groups
pub mod layout;
/// Rendering facets
pub mod scene;

use facets::{FacetId, FacetPtr};

struct Rendering {
    size: Size,
    previous_rasters: Vec<Raster>,
}

impl Rendering {
    fn new() -> Rendering {
        Rendering { previous_rasters: Vec::new(), size: Size::zero() }
    }
}

fn raster_for_corner_knockouts(
    bounds: &Rect,
    corner_radius: Coord,
    render_context: &mut RenderContext,
) -> Raster {
    let path = path_for_corner_knockouts(bounds, corner_radius, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, None);
    raster_builder.build()
}

#[derive(Default)]
pub(crate) struct IdGenerator {}

impl Iterator for IdGenerator {
    type Item = usize;

    fn next(&mut self) -> Option<usize> {
        static NEXT_ID: AtomicUsize = AtomicUsize::new(100);
        let id = NEXT_ID.fetch_add(1, Ordering::SeqCst);
        // fetch_add wraps on overflow, which we'll use as a signal
        // that this generator is out of ids.
        if id == 0 {
            None
        } else {
            Some(id)
        }
    }
}

struct FacetEntry {
    facet: FacetPtr,
    location: Point,
}

type FacetMap = BTreeMap<FacetId, FacetEntry>;

/// Group of layers created and modified by facets.
pub struct LayerGroup(Vec<Layer>);

impl LayerGroup {
    /// Replace all the layers in a layer group with the contents of the iterator.
    /// Eventually layer group might provide more incremental ways to modify the contents of
    /// the group.
    pub fn replace_all(&mut self, new_layers: impl IntoIterator<Item = Layer>) {
        self.0 = new_layers.into_iter().collect();
    }
}

#[cfg(test)]
mod test {
    use crate::{
        render::Context as RenderContext,
        scene::{
            facets::{Facet, FacetId},
            scene::SceneBuilder,
            LayerGroup,
        },
        Rect, Size,
    };
    use anyhow::Error;
    use euclid::{point2, size2};
    use itertools::assert_equal;

    struct TestFacet {
        size: Size,
    }

    impl TestFacet {
        fn new(size: Size) -> Self {
            Self { size }
        }
    }

    impl Facet for TestFacet {
        fn update_layers(
            &mut self,
            _size: Size,
            _layer_group: &mut LayerGroup,
            _render_context: &mut RenderContext,
        ) -> Result<(), Error> {
            Ok(())
        }

        fn get_size(&self) -> Size {
            self.size
        }
    }

    fn build_test_facet(builder: &mut SceneBuilder, size: Size) -> FacetId {
        let facet = TestFacet::new(size);
        builder.facet(Box::new(facet))
    }

    #[test]
    fn stack_two_boxes() {
        const OUTER_FACET_SIZE: Size = size2(300.0, 100.0);
        const INNER_FACET_SIZE: Size = size2(200.0, 50.0);
        let mut builder = SceneBuilder::new();
        builder.group().stack().expand().contents(|builder| {
            let _outer = build_test_facet(builder, OUTER_FACET_SIZE);
            let _inner = build_test_facet(builder, INNER_FACET_SIZE);
        });
        let mut scene = builder.build();

        scene.layout(size2(800.00, 600.00));

        let bounds = scene.all_facet_bounds();
        let expected_bounds = vec![
            Rect::new(point2(250.0, 250.0), OUTER_FACET_SIZE),
            Rect::new(point2(300.0, 275.0), INNER_FACET_SIZE),
        ];
        assert_equal(bounds, expected_bounds);
    }
}
