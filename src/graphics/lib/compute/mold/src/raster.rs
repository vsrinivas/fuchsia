// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::{Ref, RefCell},
    rc::Rc,
};

use crate::{
    edge::Edge,
    path::Path,
    point::Point,
    tile::{TileContour, TileContourBuilder},
    PIXEL_WIDTH,
};

#[doc(hidden)]
#[derive(Debug)]
pub struct RasterInner {
    edges: Vec<Edge<i32>>,
    translation: Point<i32>,
    new_edges: bool,
    tile_contour: TileContour,
}

#[derive(Clone, Debug)]
pub struct Raster {
    #[doc(hidden)]
    pub inner: Rc<RefCell<RasterInner>>,
}

impl Raster {
    fn from_edges(edges: Vec<Edge<i32>>) -> Self {
        let mut tile_contour_builder = TileContourBuilder::new();

        for edge in &edges {
            tile_contour_builder.enclose(edge);
        }

        let tile_contour = tile_contour_builder.build();

        Self {
            inner: Rc::new(RefCell::new(RasterInner {
                edges,
                translation: Point::new(0, 0),
                new_edges: true,
                tile_contour,
            })),
        }
    }

    pub fn new(path: &Path) -> Self {
        Self::from_edges(path.edges().flat_map(|edge| edge.to_sp_edges()).flatten().collect())
    }

    pub fn with_transform(path: &Path, transform: &[f32; 9]) -> Self {
        Self::from_edges(
            path.transformed(transform).flat_map(|edge| edge.to_sp_edges()).flatten().collect(),
        )
    }

    pub fn empty() -> Self {
        Self::from_edges(vec![])
    }

    pub(crate) fn maxed() -> Self {
        let inner = RasterInner {
            edges: vec![],
            translation: Point::new(0, 0),
            new_edges: true,
            tile_contour: TileContourBuilder::maxed(),
        };

        Self { inner: Rc::new(RefCell::new(inner)) }
    }

    pub fn from_paths<'a, I>(paths: I) -> Self
    where
        I: IntoIterator<Item = &'a Path>,
    {
        Self::from_edges(
            paths
                .into_iter()
                .map(Path::edges)
                .flatten()
                .flat_map(|edge| edge.to_sp_edges())
                .flatten()
                .collect(),
        )
    }

    pub fn from_paths_and_transforms<'a, I>(paths: I) -> Self
    where
        I: IntoIterator<Item = (&'a Path, &'a [f32; 9])>,
    {
        Self::from_edges(
            paths
                .into_iter()
                .map(|(path, transform)| path.transformed(transform))
                .flatten()
                .flat_map(|edge| edge.to_sp_edges())
                .flatten()
                .collect(),
        )
    }

    pub fn translate(&mut self, translation: Point<i32>) {
        let translation = {
            let inner = self.inner.borrow();
            Point::new(inner.translation.x + translation.x, inner.translation.y + translation.y)
        };
        self.set_translation(translation);
    }

    pub fn set_translation(&mut self, translation: Point<i32>) {
        let mut inner = self.inner.borrow_mut();

        if inner.translation != translation {
            let delta = Point::new(
                translation.x - inner.translation.x,
                translation.y - inner.translation.y,
            );

            inner.translation = translation;
            let translation = Point::new(delta.x * PIXEL_WIDTH, delta.y * PIXEL_WIDTH);

            let mut tile_contour_builder = TileContourBuilder::new();

            for edge in &mut inner.edges {
                *edge = edge.translate(translation);
                tile_contour_builder.enclose(edge);
            }

            inner.tile_contour = tile_contour_builder.build();
            inner.new_edges = true;
        }
    }

    pub fn union(&self, other: &Self) -> Self {
        let inner = self.inner.borrow();
        let other_inner = other.inner.borrow();

        let edges = inner.edges.iter().cloned().chain(other_inner.edges.iter().cloned()).collect();
        let new_edges = inner.new_edges || other_inner.new_edges;
        let tile_contour = inner.tile_contour.union(&other_inner.tile_contour);

        Self {
            inner: Rc::new(RefCell::new(RasterInner {
                edges,
                translation: Point::new(0, 0),
                new_edges,
                tile_contour,
            })),
        }
    }

    pub(crate) fn new_edges(&self) -> bool {
        let inner = self.inner.borrow();
        inner.new_edges
    }

    pub(crate) fn edges(&self) -> Ref<[Edge<i32>]> {
        self.inner.borrow_mut().new_edges = false;
        Ref::map(self.inner.borrow(), |inner| &inner.edges[..])
    }

    pub(crate) fn tile_contour(&self) -> Ref<TileContour> {
        Ref::map(self.inner.borrow(), |inner| &inner.tile_contour)
    }
}

impl Eq for Raster {}

impl PartialEq for Raster {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner)
    }
}
