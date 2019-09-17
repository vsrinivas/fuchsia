// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::rc::Rc;

use crate::{
    edge::Edge,
    path::Path,
    point::Point,
    tile::{TileContour, TileContourBuilder},
};

#[doc(hidden)]
#[derive(Debug)]
pub struct RasterInner {
    edges: Vec<Edge<i32>>,
    tile_contour: TileContour,
}

#[derive(Clone, Debug)]
pub struct Raster {
    #[doc(hidden)]
    pub inner: Rc<RasterInner>,
    translation: Point<i32>,
    translated_tile_contour: Option<TileContour>,
}

impl Raster {
    fn from_edges(edges: Vec<Edge<i32>>) -> Self {
        let mut tile_contour_builder = TileContourBuilder::new();

        for edge in &edges {
            tile_contour_builder.enclose(edge);
        }

        let tile_contour = tile_contour_builder.build();

        Self {
            inner: Rc::new(RasterInner { edges, tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
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
        let inner = RasterInner { edges: vec![], tile_contour: TileContourBuilder::maxed() };

        Self { inner: Rc::new(inner), translation: Point::new(0, 0), translated_tile_contour: None }
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
        let translation =
            { Point::new(self.translation.x + translation.x, self.translation.y + translation.y) };
        self.set_translation(translation);
    }

    pub fn set_translation(&mut self, translation: Point<i32>) {
        let inner = &self.inner;

        if self.translation != translation {
            self.translation = translation;
            self.translated_tile_contour = Some(inner.tile_contour.translated(translation));
        }
    }

    pub fn union(&self, other: &Self) -> Self {
        let inner = &self.inner;
        let other_inner = &other.inner;

        let edges = inner
            .edges
            .iter()
            .cloned()
            .map(|edge| edge.translate(self.translation))
            .chain(other_inner.edges.iter().cloned().map(|edge| edge.translate(other.translation)))
            .collect();
        let tile_contour = inner.tile_contour.union(&other_inner.tile_contour);

        Self {
            inner: Rc::new(RasterInner { edges, tile_contour }),
            translation: Point::new(0, 0),
            translated_tile_contour: None,
        }
    }

    pub(crate) fn edges(&self) -> &[Edge<i32>] {
        &self.inner.edges[..]
    }

    pub(crate) fn tile_contour(&self) -> &TileContour {
        self.translated_tile_contour.as_ref().unwrap_or(&self.inner.tile_contour)
    }

    pub(crate) fn translation(&self) -> Point<i32> {
        self.translation
    }
}

impl Eq for Raster {}

impl PartialEq for Raster {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner) && self.translation == other.translation
    }
}
