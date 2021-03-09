// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![macro_use]

/// Macro for generating multiple tests from a common test function
/// # Example
/// ```
/// async_property_test!(test_to_run,
///     case1(0, String::from("abc")),
///     case2(1, String::from("xyz")),
/// );
/// async fn test_to_run(prop1: usize, prop2: String) {
///     assert!(prop1 < 2);
///     assert_eq!(prop2.chars().len(), 3);
/// }
/// ```
macro_rules! async_property_test {
    ($test_func:ident => [$($test_name:ident($($args:expr),+$(,)?)),+$(,)?]) => {
        $(paste::paste!{
            #[allow(non_snake_case)]
            #[fuchsia_async::run_until_stalled(test)]
            async fn [<$test_func ___ $test_name>]() {
                $test_func($($args,)+).await;
            }
        })+
    }
}

mod accessibility_tests;
mod agent_tests;
mod audio_policy_handler_tests;
mod audio_policy_tests;
mod audio_tests;
mod bluetooth_earcons_tests;
mod camera_watcher_agent_tests;
mod device_tests;
mod display_tests;
mod do_not_disturb_tests;
mod event_tests;
mod factory_reset_tests;
pub mod fakes;
mod fidl_processor_tests;
mod hanging_get_tests;
mod input_test_environment;
mod input_tests;
mod intl_tests;
mod light_sensor_tests;
mod light_tests;
mod media_buttons_agent_tests;
mod message_tests;
pub(crate) mod message_utils;
mod night_mode_tests;
mod policy_handler_tests;
mod policy_proxy_tests;
mod privacy_tests;
mod resource_monitor_tests;
mod restore_agent_tests;
mod scaffold;
mod service_configuration_tests;
mod setting_handler_tests;
mod setting_proxy_tests;
mod setup_tests;
mod stream_volume_tests;
mod test_failure_utils;
mod volume_change_earcons_tests;
