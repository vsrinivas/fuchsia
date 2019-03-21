// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{Point, Rect, Size};
use fidl_fuchsia_ui_gfx::{BoundingBox, Vec3, ViewProperties};
use fuchsia_scenic::{EntityNode, ViewHolder};

/// Container for data related to a single child view displaying an emulated session.
pub struct ChildViewData {
    bounds: Option<Rect>,
    host_node: EntityNode,
    host_view_holder: ViewHolder,
}

impl ChildViewData {
    pub fn new(host_node: EntityNode, host_view_holder: ViewHolder) -> ChildViewData {
        ChildViewData { bounds: None, host_node: host_node, host_view_holder: host_view_holder }
    }

    pub fn id(&self) -> u32 {
        self.host_view_holder.id()
    }
}

/// Lays out the given child views using the given container.
///
/// Voila uses a column layout to display 2 or more emulated sessions side by side.
pub fn layout(child_views: &mut [&mut ChildViewData], size: &Size) -> Result<(), failure::Error> {
    if child_views.is_empty() {
        return Ok(());
    }
    let num_views = child_views.len();

    let tile_height = size.height;
    let tile_width = (size.width / num_views as f32).floor();
    for (column_index, view) in child_views.iter_mut().enumerate() {
        let tile_bounds = Rect::new(
            Point::new(column_index as f32 * tile_width, 0.0),
            Size::new(tile_width, tile_height),
        );
        let tile_bounds = inset(&tile_bounds, 5.0);
        let view_properties = ViewProperties {
            bounding_box: BoundingBox {
                min: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
                max: Vec3 { x: tile_bounds.size.width, y: tile_bounds.size.height, z: 0.0 },
            },
            inset_from_min: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            inset_from_max: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            focus_change: true,
            downward_input: false,
        };
        view.host_view_holder.set_view_properties(view_properties);
        view.host_node.set_translation(tile_bounds.origin.x, tile_bounds.origin.y, 0.0);
        view.bounds = Some(tile_bounds);
    }
    Ok(())
}

/// Reduces the given bounds to leave the given amount of padding around it.
///
/// The added padding is no bigger than a third of the smaller dimension of the
/// bounds.
fn inset(rect: &Rect, padding: f32) -> Rect {
    let inset = padding.min(rect.size.width / 3.0).min(rect.size.height / 3.0);
    let double_inset = inset * 2.0;
    Rect::new(
        Point::new(rect.origin.x + inset, rect.origin.y + inset),
        Size::new(rect.size.width - double_inset, rect.size.height - double_inset),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn inset_empty_returns_empty() {
        let empty = Rect::zero();

        let result = inset(&empty, 2.0);

        assert_eq!(result, Rect::zero());
    }

    #[test]
    fn inset_non_empty_works_correctly() {
        let bounds = Rect::new(Point::new(1.0, 3.0), Size::new(10.0, 8.0));

        let result = inset(&bounds, 2.0);

        assert_eq!(result.origin.x, 3.0);
        assert_eq!(result.origin.y, 5.0);
        assert_eq!(result.size.width, 6.0);
        assert_eq!(result.size.height, 4.0);
    }

    #[test]
    fn inset_padding_is_at_most_a_third_of_dimension() {
        let bounds = Rect::new(Point::new(0.0, 0.0), Size::new(9.0, 10.0));

        let result = inset(&bounds, 5.0);

        // Verify that the actual padding was 3.0, not 5.0.
        assert_eq!(result.origin.x, 3.0);
        assert_eq!(result.origin.y, 3.0);
        assert_eq!(result.size.width, 3.0);
        assert_eq!(result.size.height, 4.0);
    }
}
