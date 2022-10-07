// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::StringReference;

pub(super) const ARENA_LOG_ROOT: &'static str = "gestures_event_log";

// Some inspect keys are repeated for every event, which makes for an
// inefficient use of space in the Inspect VMO. Optimize that space by
// using `StringReference`s.
lazy_static::lazy_static! {
    // Event types.
    pub(super) static ref TOUCHPAD_EVENT_NODE: StringReference<'static> = "touchpad_event".into();
    pub(super) static ref KEY_EVENT_NODE: StringReference<'static> = "key_event".into();
    pub(super) static ref MISMATCH_EVENT_NODE: StringReference<'static> = "mismatch_event".into();
    pub(super) static ref GESTURE_START_NODE: StringReference<'static> = "gesture_start".into();
    pub(super) static ref GESTURE_END_NODE: StringReference<'static> = "gesture_end".into();

    // Device-event properties. Common to `TOUCHPAD_EVENT_NODE` and `KEY_EVENT_NODE`.
    pub(super) static ref EVENT_TIME_PROP: StringReference<'static> = "driver_monotonic_nanos".into();
    pub(super) static ref ENTRY_LATENCY_PROP: StringReference<'static> = "entry_latency_micros".into();

    // Touchpad-specific properties.
    pub(super) static ref PRESSED_BUTTONS_PROP: StringReference<'static> = "pressed_buttons".into();
    pub(super) static ref CONTACT_STATE_PROP: StringReference<'static> = "contacts".into();
    pub(super) static ref X_POS_PROP: StringReference<'static> = "pos_x_mm".into();
    pub(super) static ref Y_POS_PROP: StringReference<'static> = "pos_y_mm".into();
    pub(super) static ref WIDTH_PROP: StringReference<'static> = "width_mm".into();
    pub(super) static ref HEIGHT_PROP: StringReference<'static> = "height_mm".into();

    // Reason properties. Shared between `MISMATCH_EVENT_NODE`s, and
    // `GESTURE_END_NODE`s.
    pub(super) static ref REASON_PROP: StringReference<'static> = "reason".into();
    pub(super) static ref CONTENDER_PROP: StringReference<'static> = "contender".into();
    pub(super) static ref CRITERION_PROP: StringReference<'static> = "criterion".into();
    pub(super) static ref ACTUAL_VALUE_PROP: StringReference<'static> = "actual".into();
    pub(super) static ref MIN_VALUE_PROP: StringReference<'static> = "min_allowed".into();
    pub(super) static ref MAX_VALUE_PROP: StringReference<'static> = "max_allowed".into();

    // Properties shared by gesture start and gesture end events.
    pub(super) static ref NAME_PROP: StringReference<'static> = "gesture_name".into();

    // Gesture start properties.
    //
    // *Note*
    // * These latencies are relative to touchpad event which started the contest.
    // * The latency as the delta between the driver timestamps for the relevant
    //   touchpad events. Monotonic time duration might differ.
    pub(super) static ref TIME_LATENCY_PROP: StringReference<'static> = "latency_micros".into();
    pub(super) static ref EVENT_LATENCY_PROP: StringReference<'static> = "latency_event_count".into();

    // Gesture end properties.
    //
    // *Note*
    // * These durations are relative to touchpad event which resulted in a
    //   gesture start event. That means, for example, that the durations for
    //   tap events are zero.
    // * These durations are for the (touchpad) events which serve as inputs to
    //   the gesture arena. The durations for the mouse events that the gesture
    //   arena outputs may differ.
    // * The time duration is measured as the delta between the driver timestamps
    //   for the touchpad events which triggered the start and end of the gesture.
    //   Monotonic time duration might differ.
    pub(super) static ref TIME_DURATION_PROP: StringReference<'static> = "duration_micros".into();
    pub(super) static ref EVENT_DURATION_PROP: StringReference<'static> = "event_count".into();
}

// Example JSON dump of inspect tree generated by `gesture_arena`, from a unit test:
//
// ```json
// {
//   "root": {
//     "gestures_event_log": {
//       "0": {
//         "touchpad_event": {
//           "driver_monotonic_nanos": 12300,
//           "entry_latency_micros": 9987,
//           "pressed_buttons": [
//             1
//           ],
//           "contacts": {
//             "1": {
//               "pos_x_mm": 2.0,
//               "pos_y_mm": 3.0
//             },
//             "2": {
//               "pos_x_mm": 40.0,
//               "pos_y_mm": 50.0
//             }
//           }
//         }
//       },
//       "1": {
//         "mismatch_event": {
//           "contender": "utils::StubContender",
//           "reason": "some reason"
//         }
//       },
//       "2": {
//         "mismatch_event": {
//           "actual": 42,
//           "contender": "utils::StubContender",
//           "criterion": "num_goats_teleported",
//           "max_allowed": 30,
//           "min_allowed": 10
//         }
//       },
//       "3": {
//         "mismatch_event": {
//           "actual": 42.0,
//           "contender": "utils::StubContender",
//           "criterion": "teleportation_distance_kilometers",
//           "max_allowed": 30.5,
//           "min_allowed": 10.125
//         }
//       },
//       "4": {
//         "mismatch_event": {
//           "actual": -42,
//           "contender": "utils::StubContender",
//           "criterion": "budget_surplus_trillions",
//           "max_allowed": 1,
//           "min_allowed": -10
//         }
//       },
//       "5": {
//         "key_event": {
//           "driver_monotonic_nanos": 11000000,
//           "entry_latency_micros": 1000
//         }
//       },
//       "6": {
//         "key_event": {
//           "driver_monotonic_nanos": 13000000,
//           "entry_latency_micros": 1000
//         }
//       },
//       "7": {
//         "touchpad_event": {
//           "driver_monotonic_nanos": 18000000,
//           "entry_latency_micros": 1000,
//           "pressed_buttons": [],
//           "contacts": {
//             "1": {
//               "height_mm": 4.0,
//               "pos_x_mm": 2.0,
//               "pos_y_mm": 3.0,
//               "width_mm": 3.0
//             }
//           }
//         }
//       },
//       "8": {
//         "gesture_start": {
//           "gesture_name": "click",
//           "latency_event_count": 1,
//           "latency_micros": 17987
//         }
//       },
//       "9": {
//         "gesture_end": {
//           "contender": "utils::StubMatchedContender",
//           "duration_micros": 0,
//           "event_count": 0,
//           "gesture_name": "click",
//           "reason": "discrete-recognizer"
//         }
//       }
//     }
//   }
// }
// ```

// Example `iquery` excerpt from a live device:
//
// ```json5
// core/ui/scene_manager:
//   metadata:
//     filename = fuchsia.inspect.Tree
//     component_url = fuchsia-pkg://fuchsia.com/scene_manager#meta/scene_manager.cm
//     timestamp = 375999103371
//   payload:
//     root:
//       fuchsia.inspect.Stats:
//         allocated_blocks = 9998
//         current_size = 163840
//         deallocated_blocks = 0
//         failed_allocations = 0
//         maximum_size = 307200
//         total_dynamic_children = 1
//       input_pipeline:
//         gestures_event_log:
//           0:
//             key_event:
//               driver_monotonic_nanos = 297873226402
//               entry_latency_micros = 32908
//           1:
//             key_event:
//               driver_monotonic_nanos = 297955554861
//               entry_latency_micros = 1403
//           /* ...many entries omitted... */
//           150:
//             touchpad_event:
//               driver_monotonic_nanos = 361816423302
//               entry_latency_micros = 14432
//               pressed_buttons = []
//               contacts:
//                 0:
//                   height_mm = 2.5840000
//                   pos_x_mm = 26.528000
//                   pos_y_mm = 23.712999
//                   width_mm = 2.9530000
//           /* mismatches on u64 properties */
//           151:
//             mismatch_event:
//               actual = 0
//               contender = one_finger_drag::InitialContender
//               criterion = num_pressed_buttons
//               max_allowed = 1
//               min_allowed = 1
//           152:
//             mismatch_event:
//               actual = 1
//               contender = scroll::InitialContender
//               criterion = num_contacts
//               max_allowed = 2
//               min_allowed = 2
//           /* ... many entries omitted ... */
//           159:
//             touchpad_event:
//               driver_monotonic_nanos = 361871136901
//               entry_latency_micros = 4745
//               pressed_buttons = []
//               contacts:
//                 0:
//                   height_mm = 2.5840000
//                   pos_x_mm = 27.162001
//                   pos_y_mm = 24.061001
//                   width_mm = 2.9530000
//           /* mismatches on float properties */
//           160:
//             mismatch_event:
//               actual = 0.723230
//               contender = click::UnpressedContender
//               criterion = displacement_mm
//               max_allowed = 0.500000
//           161:
//             mismatch_event:
//               actual = 0.723230
//               contender = primary_tap::FingerContactContender
//               criterion = displacement_mm
//               max_allowed = 0.500000
//           162:
//             mismatch_event:
//               actual = 0.723230
//               contender = secondary_tap::OneFingerContactContender
//               criterion = displacement_mm
//               max_allowed = 0.500000
//           /* gesture start */
//           163:
//             gesture_start:
//               gesture_name = motion
//               latency_event_count = 7
//               latency_micros = 54713
//           /* ... many entries omitted ... */
//           295:
//             touchpad_event:
//               driver_monotonic_nanos = 362903529428
//               entry_latency_micros = 3603
//               pressed_buttons = []
//               contacts:
//           /* gesture end */
//           296:
//             gesture_end:
//               actual = 0
//               contender = motion::Winner
//               criterion = num_contacts
//               duration_micros = 1032392
//               event_count = 132
//               gesture_name = motion
//               max_allowed = 1
//               min_allowed = 1
//           /* ... many entries omitted ... */
//           596:
//             touchpad_event:
//               driver_monotonic_nanos = 370902306135
//               entry_latency_micros = 4630
//               pressed_buttons = []
//               contacts:
//                 0:
//                   height_mm = 2.5840000
//                   pos_x_mm = 76.887001
//                   pos_y_mm = 25.962999
//                   width_mm = 2.9530000
//           /* ... many entries omitted ... */
//           752:
//             touchpad_event:
//               driver_monotonic_nanos = 372106779670
//               entry_latency_micros = 4607
//               pressed_buttons = []
//               contacts:
//                 0:
//                   height_mm = 3.2300000
//                   pos_x_mm = 76.949997
//                   pos_y_mm = 26.184999
//                   width_mm = 2.9530000
//           /* mismatches on i64 properties */
//           753:
//             mismatch_event:
//               actual = 1204473
//               contender = primary_tap::FingerContactContender
//               criterion = elapsed_time_micros
//               max_allowed = 1200000
//           754:
//             mismatch_event:
//               actual = 1204473
//               contender = secondary_tap::OneFingerContactContender
//               criterion = elapsed_time_micros
//               max_allowed = 1200000
// ```
