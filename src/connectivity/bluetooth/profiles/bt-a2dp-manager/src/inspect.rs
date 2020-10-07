// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_a2dp::Role,
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, Node, NumericProperty, Property},
    fuchsia_inspect_contrib::nodes::NodeExt,
    fuchsia_inspect_derive::{AttachError, Inspect},
    fuchsia_zircon as zx,
    futures::FutureExt,
    std::sync::Arc,
};

use crate::util;

/// An inspect node that represents information on the mode of operation for the A2DP Profile.
#[derive(Default)]
pub struct A2dpManagerInspect {
    /// The current A2DP role
    role: inspect::StringProperty,
    /// Total number of times the role has been set to a new role.
    role_set_count: inspect::UintProperty,
    /// Time that this role was set. This time is not updated if the role was already set to the
    /// requested role. Managed manually.
    role_set_at: Option<fuchsia_inspect_contrib::nodes::TimeProperty>,
    /// Shared reference to the `Time` that the current role was set. Used to lazily calculate the
    /// length of time between the instant that the role was set and the time the inspect value is
    /// queried.
    set_at: Arc<futures::lock::Mutex<Option<fasync::Time>>>,
    inspect_node: inspect::Node,
}

impl Inspect for &mut A2dpManagerInspect {
    fn iattach<'a>(self, parent: &'a Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name);
        self.role = self.inspect_node.create_string("role", "Not Set");
        self.role_set_count = self.inspect_node.create_uint("role_set_count", 0);
        self.set_at = Arc::new(futures::lock::Mutex::new(None));
        let set_at_reader = self.set_at.clone();
        self.inspect_node.record_lazy_values("time_since_role_set", move || {
            let set_at_reader = set_at_reader.clone();
            async move {
                let inspector = inspect::Inspector::new();
                if let Some(set_at) = *set_at_reader.lock().await {
                    let time = duration_to_formatted_seconds(fasync::Time::now() - set_at);
                    inspector.root().record_string("time_since_role_set", time);
                }
                Ok(inspector)
            }
            .boxed()
        });

        Ok(())
    }
}

impl A2dpManagerInspect {
    /// Set the role in inspect. This should only be called when the role changes.
    pub async fn set_role(&mut self, role: Role) {
        self.role.set(util::to_display_str(role));
        self.role_set_count.add(1);
        let now = fasync::Time::now();
        if let Some(prop) = &self.role_set_at {
            prop.set_at(now.into());
        } else {
            self.role_set_at =
                Some(self.inspect_node.create_time_at("role_set_at_time", now.into()));
        }
        *self.set_at.lock().await = Some(now);
    }
}

fn duration_to_formatted_seconds(duration: zx::Duration) -> String {
    let seconds = duration.into_seconds();
    let millis = duration.into_millis() % 1000;
    format!("{}.{:03}", seconds, millis)
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_utils::PollExt;
    use fuchsia_async::DurationExt;
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::WithInspect;
    use fuchsia_zircon::DurationNum;
    use futures::pin_mut;

    #[test]
    fn inspect_tree() {
        let mut exec = fasync::Executor::new_with_fake_time().unwrap();

        // T = 1.2345
        exec.set_fake_time(fasync::Time::from_nanos(1_234500000));

        let inspector = inspect::component::inspector();
        let root = inspector.root();

        let mut inspect =
            A2dpManagerInspect::default().with_inspect(&root, "operating_mode").unwrap();

        // Default inspect state.
        assert_inspect_tree!(inspector, root: {
            operating_mode: {
                role: "Not Set",
                role_set_count: 0u64,
            }
        });

        {
            let fut = inspect.set_role(Role::Sink);
            pin_mut!(fut);
            exec.run_until_stalled(&mut fut).unwrap();
        }

        // Inspect when role is set.
        assert_inspect_tree!(inspector, root: {
            operating_mode: {
                role: "Sink",
                role_set_count: 1u64,
                role_set_at_time: 1_234500000i64,
                time_since_role_set: "0.000",
            }
        });

        // T = 6.2345
        exec.set_fake_time(5.seconds().after_now());

        // Inspect after time passes without a state change.
        assert_inspect_tree!(inspector, root: {
            operating_mode: {
                role: "Sink",
                role_set_count: 1u64,
                role_set_at_time: 1_234500000i64,
                time_since_role_set: "5.000",
            }
        });

        // T = 7.2345
        exec.set_fake_time(1.seconds().after_now());

        {
            let fut = inspect.set_role(Role::Source);
            pin_mut!(fut);
            exec.run_until_stalled(&mut fut).unwrap();
        }

        // T = 9.2345
        exec.set_fake_time(2_123.millis().after_now());

        // Inspect after role is changed and more time passes.
        assert_inspect_tree!(inspector, root: {
            operating_mode: {
                role: "Source",
                role_set_count: 2u64,
                role_set_at_time: 7_234500000i64,
                time_since_role_set: "2.123",
            }
        });
    }
}
