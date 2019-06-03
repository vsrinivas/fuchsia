// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Constructs a suggestion with common fields.
/// Remember to `use crate::models::{Suggestion, DisplayInfo, Intent, AddMod};`
#[macro_export]
macro_rules! suggestion {
    (
        action = $action:expr, title = $title:expr,
        parameters = [
            $((
                name = $name:expr,
                entity_reference = $reference:expr
              )
             ),*
        ]
    ) => {
        Suggestion::new(
            AddMod::new(
                Intent::new()
                    .with_action($action)
                    $(.add_parameter($name, $reference))*,
                None,
            ),
            DisplayInfo::new().with_title($title),
            Some("story_name".to_string()),
        )
    }
}
