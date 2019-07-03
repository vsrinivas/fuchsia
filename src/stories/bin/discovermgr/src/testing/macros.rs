// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Constructs a suggestion with common fields.
/// Remember to `use crate::models::{Suggestion, DisplayInfo, Intent, AddMod};`
#[macro_export]
macro_rules! suggestion {
    // Match without icon
    (
        action = $action:expr,
        title = $title:expr,
        parameters = [$($rest:tt)*],
        story = $story:expr
    ) => {
        create_suggestion!(
            action = $action,
            parameters = [$($rest)*], story = $story,
            display_info = DisplayInfo::new().with_title($title),
        )
    };

    // Match with icon
    (
        action = $action:expr,
        title = $title:expr,
        icon = $icon:expr,
        parameters = [$($rest:tt)*],
        story = $story:expr
    ) => {
        create_suggestion!(
            action = $action,
            parameters = [$($rest)*], story = $story,
            display_info = DisplayInfo::new().with_icon($icon).with_title($title),
        )
    };
}

macro_rules! create_suggestion {
    (
        action = $action:expr,
        parameters = [
            $((
                name = $name:expr,
                entity_reference = $reference:expr
              )
             ),*
        ],
        story = $story:expr,
        display_info = $display_info:expr,
    ) => {
        Suggestion::new(
            AddMod::new(
                Intent::new()
                    .with_action($action)
                    $(.add_parameter($name, $reference))*,
                Some($story.to_string()),
                None,
            ),
            $display_info
        )
    };
}
