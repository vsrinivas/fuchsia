// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{Point, Rect, Size};
use fidl_fuchsia_ui_gfx::{BoundingBox, Vec3, ViewProperties};
use fuchsia_scenic::{EntityNode, SessionPtr, ViewHolder};

use crate::{toggle::Toggle, REPLICA_Z};

const CONTROLLER_VIEW_HEIGHT: f32 = 25.0;

/// Container for data related to a single child view displaying an emulated session.
pub struct ChildViewData {
    bounds: Option<Rect>,
    host_node: EntityNode,
    host_view_holder: ViewHolder,
    pub toggle: Toggle,
}

impl ChildViewData {
    pub fn new(
        host_node: EntityNode,
        host_view_holder: ViewHolder,
        toggle: Toggle,
    ) -> ChildViewData {
        ChildViewData {
            bounds: None,
            host_node: host_node,
            host_view_holder: host_view_holder,
            toggle: toggle,
        }
    }

    pub fn id(&self) -> u32 {
        self.host_view_holder.id()
    }
}

/// Lays out the given child views using the given container.
///
/// Voila uses a column layout to display 2 or more emulated sessions side by side.
pub fn layout(
    child_views: &mut [&mut ChildViewData],
    size: &Size,
    session: &SessionPtr,
) -> Result<(), failure::Error> {
    if child_views.is_empty() {
        return Ok(());
    }
    let num_views = child_views.len();

    let tile_width = (size.width / num_views as f32).floor();
    let tile_height = size.height;
    for (column_index, view) in child_views.iter_mut().enumerate() {
        let tile_bounds = Rect::new(
            Point::new(column_index as f32 * tile_width, 0.0),
            Size::new(tile_width, tile_height),
        );
        layout_replica(view, &tile_bounds, session)?;
    }
    Ok(())
}

fn layout_replica(
    view: &mut ChildViewData,
    bounds: &Rect,
    session: &SessionPtr,
) -> Result<(), failure::Error> {
    let (replica_bounds, controller_bounds) = split_bounds(&bounds, CONTROLLER_VIEW_HEIGHT);

    // Update the session view.
    let replica_bounds = inset(&replica_bounds, 5.0);
    let view_properties = get_replica_view_properties(&replica_bounds);
    view.host_view_holder.set_view_properties(view_properties);
    view.host_node.set_translation(replica_bounds.origin.x, replica_bounds.origin.y, REPLICA_Z);
    view.bounds = Some(replica_bounds);

    // Update the controller view.
    match controller_bounds {
        None => {
            // TODO(ppi): hide the controller when it doesn't fit on the screen.
        }
        Some(rect) => view.toggle.update(&inset(&rect, 5.0), session)?,
    }

    Ok(())
}

/// Splits the screen area available for one replica into a replica view and a controller view.
fn split_bounds(bounds: &Rect, controller_view_height: f32) -> (Rect, Option<Rect>) {
    if bounds.size.height < controller_view_height * 2.0 {
        return ((*bounds).clone(), None);
    }
    let replica_bounds = Rect::new(
        bounds.origin.clone(),
        Size::new(bounds.size.width, bounds.size.height - controller_view_height),
    );
    let controller_bounds = Rect::new(
        Point::new(bounds.origin.x, bounds.origin.y + bounds.size.height - controller_view_height),
        Size::new(bounds.size.width, controller_view_height),
    );
    (replica_bounds, Some(controller_bounds))
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

/// Returns the view properties to use for a replica, given the intended bounds.
fn get_replica_view_properties(bounds: &Rect) -> ViewProperties {
    ViewProperties {
        bounding_box: BoundingBox {
            min: Vec3 { x: 0.0, y: 0.0, z: -1000.0 },
            max: Vec3 { x: bounds.size.width, y: bounds.size.height, z: 0.0 },
        },
        inset_from_min: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
        inset_from_max: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
        focus_change: true,
        downward_input: false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn split_bounds_works_correctly() {
        let bounds = Rect::new(Point::new(10.0, 20.0), Size::new(75.0, 80.0));

        let (replica_bounds, maybe_controller_bounds) = split_bounds(&bounds, 10.0);

        assert_eq!(replica_bounds.origin.x, 10.0);
        assert_eq!(replica_bounds.origin.y, 20.0);
        assert_eq!(replica_bounds.size.width, 75.0);
        assert_eq!(replica_bounds.size.height, 70.0);

        assert_eq!(maybe_controller_bounds.is_some(), true);
        let controller_bounds = maybe_controller_bounds.expect("expected bounds to be present");
        assert_eq!(controller_bounds.origin.x, 10.0);
        assert_eq!(controller_bounds.origin.y, 90.0);
        assert_eq!(controller_bounds.size.width, 75.0);
        assert_eq!(controller_bounds.size.height, 10.0);
    }

    #[test]
    fn split_bounds_with_no_space_for_controller_hides_controller() {
        let bounds = Rect::new(Point::new(0.0, 0.0), Size::new(10.0, 10.0));

        let (_replica_bounds, maybe_controller_bounds) = split_bounds(&bounds, 6.0);
        assert_eq!(maybe_controller_bounds.is_none(), true);
    }

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

    /// Verifies that all dimensions of the resulting view bounding box are
    /// non-zero.
    ///
    /// This is a regression test for LE-746 - while having the Z-dimension
    /// equal to 0 was OK for Scenic, it made the intermediary view embedded by
    /// sessionmgr not update.
    #[test]
    fn get_replica_view_properties_returns_non_zero_box() {
        let bounds = Rect::new(Point::new(0.0, 0.0), Size::new(10.0, 10.0));

        let view_properties = get_replica_view_properties(&bounds);

        assert!(view_properties.bounding_box.max.x - view_properties.bounding_box.min.x > 0.0);
        assert!(view_properties.bounding_box.max.y - view_properties.bounding_box.min.y > 0.0);
        assert!(view_properties.bounding_box.max.z - view_properties.bounding_box.min.z > 0.0);
    }
}
