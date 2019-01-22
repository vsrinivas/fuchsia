// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::OutOfLine;
use fidl_fuchsia_math::{InsetF, RectF, SizeF};
use fidl_fuchsia_ui_viewsv1::{CustomFocusBehavior, ViewLayout, ViewProperties};
use fuchsia_scenic::EntityNode;

/// Container for data related to a single child view displaying an emulated session.
pub struct ChildViewData {
    key: u32,
    bounds: Option<RectF>,
    host_node: EntityNode,
}

impl ChildViewData {
    pub fn new(key: u32, host_node: EntityNode) -> ChildViewData {
        ChildViewData {
            key: key,
            bounds: None,
            host_node: host_node,
        }
    }
}

/// Lays out the given child views using the given container.
///
/// Voila uses a column layout to display 2 or more emulated sessions side by side.
pub fn layout(
    child_views: &mut [&mut ChildViewData],
    view_container: &fidl_fuchsia_ui_viewsv1::ViewContainerProxy, width: f32, height: f32,
) {
    if child_views.is_empty() {
        return;
    }
    let num_views = child_views.len();

    let tile_height = height;
    let tile_width = (width / num_views as f32).floor();
    for (column_index, view) in child_views.iter_mut().enumerate() {
        let tile_bounds = RectF {
            height: tile_height,
            width: tile_width,
            x: column_index as f32 * tile_width,
            y: 0.0,
        };
        let tile_bounds = inset(&tile_bounds, 5.0);
        let mut view_properties = ViewProperties {
            custom_focus_behavior: Some(Box::new(CustomFocusBehavior { allow_focus: true })),
            view_layout: Some(Box::new(ViewLayout {
                inset: InsetF {
                    bottom: 0.0,
                    left: 0.0,
                    right: 0.0,
                    top: 0.0,
                },
                size: SizeF {
                    width: tile_bounds.width,
                    height: tile_bounds.height,
                },
            })),
        };
        view_container
            .set_child_properties(view.key, Some(OutOfLine(&mut view_properties)))
            .unwrap();
        view.host_node
            .set_translation(tile_bounds.x, tile_bounds.y, 0.0);
        view.bounds = Some(tile_bounds);
    }
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
        let empty = RectF {
            x: 0.0,
            y: 0.0,
            width: 0.0,
            height: 0.0,
        };

        let result = inset(&empty, 2.0);
        assert_eq!(result.x, 0.0);
        assert_eq!(result.y, 0.0);
        assert_eq!(result.width, 0.0);
        assert_eq!(result.height, 0.0);
    }

    #[test]
    fn inset_non_empty_works_correctly() {
        let empty = RectF {
            x: 1.0,
            y: 3.0,
            width: 10.0,
            height: 8.0,
        };

        let result = inset(&empty, 2.0);
        assert_eq!(result.x, 3.0);
        assert_eq!(result.y, 5.0);
        assert_eq!(result.width, 6.0);
        assert_eq!(result.height, 4.0);
    }
}
