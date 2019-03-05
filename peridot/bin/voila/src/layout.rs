// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::Size;
use fidl_fuchsia_math::RectF;
use fidl_fuchsia_ui_gfx::{BoundingBox, Vec3, ViewProperties};
use fuchsia_scenic::{EntityNode, ViewHolder};

/// Container for data related to a single child view displaying an emulated session.
pub struct ChildViewData {
    bounds: Option<RectF>,
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
        let tile_bounds = RectF {
            height: tile_height,
            width: tile_width,
            x: column_index as f32 * tile_width,
            y: 0.0,
        };
        let tile_bounds = inset(&tile_bounds, 5.0);
        let view_properties = ViewProperties {
            bounding_box: BoundingBox {
                min: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
                max: Vec3 { x: tile_bounds.width, y: tile_bounds.height, z: 0.0 },
            },
            inset_from_min: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            inset_from_max: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            focus_change: true,
            downward_input: false,
        };
        view.host_view_holder.set_view_properties(view_properties);
        view.host_node.set_translation(tile_bounds.x, tile_bounds.y, 0.0);
        view.bounds = Some(tile_bounds);
    }
    Ok(())
}

fn inset(rect: &RectF, border: f32) -> RectF {
    let inset = border.min(rect.width / 0.3).min(rect.height / 0.3);
    let double_inset = inset * 2.0;
    RectF {
        x: rect.x + inset,
        y: rect.y + inset,
        width: rect.width - double_inset,
        height: rect.height - double_inset,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn inset_empty_returns_empty() {
        let empty = RectF { x: 0.0, y: 0.0, width: 0.0, height: 0.0 };

        let result = inset(&empty, 2.0);
        assert_eq!(result.x, 0.0);
        assert_eq!(result.y, 0.0);
        assert_eq!(result.width, 0.0);
        assert_eq!(result.height, 0.0);
    }

    #[test]
    fn inset_non_empty_works_correctly() {
        let empty = RectF { x: 1.0, y: 3.0, width: 10.0, height: 8.0 };

        let result = inset(&empty, 2.0);
        assert_eq!(result.x, 3.0);
        assert_eq!(result.y, 5.0);
        assert_eq!(result.width, 6.0);
        assert_eq!(result.height, 4.0);
    }
}
