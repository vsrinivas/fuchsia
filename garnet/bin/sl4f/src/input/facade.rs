use crate::input::types::{
    ActionResult, MultiFingerSwipeRequest, MultiFingerTapRequest, SwipeRequest, TapRequest,
};
use anyhow::{Context, Error};
use fuchsia_syslog::macros::fx_log_info;
use input_synthesis as input;
use serde_json::{from_value, Value};
use std::{convert::TryFrom, time::Duration};
const DEFAULT_DIMENSION: u32 = 1000;
const DEFAULT_DURATION: u64 = 300;

macro_rules! validate_fingers {
    ( $fingers:expr, $field:ident $comparator:tt $limit:expr ) => {
        match $fingers.iter().enumerate().find(|(_, finger)| !(finger.$field $comparator $limit)) {
            None => Ok(()),
            Some((finger_num, finger)) => Err(anyhow!(
                "finger {}: expected {} {} {}, but {} is not {} {}",
                finger_num,
                stringify!($field),
                stringify!($comparator),
                stringify!($limit),
                finger.$field,
                stringify!($comparator),
                $limit
            )),
        }
    };
}

/// Perform Input fidl operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct InputFacade {}

impl InputFacade {
    pub fn new() -> InputFacade {
        InputFacade {}
    }

    /// Tap at coordinates (x, y) for a touchscreen with default or custom
    /// width, height, duration, and tap event counts
    ///
    /// # Arguments
    /// * `value`: will be parsed to TapRequest
    ///   * must include:
    ///     * `x`: X axis coordinate
    ///     * `y`: Y axis coordinate
    ///   * optionally includes any of:
    ///     * `width`: Horizontal resolution of the touch panel, defaults to 1000
    ///     * `height`: Vertical resolution of the touch panel, defaults to 1000
    ///     * `tap_event_count`: Number of tap events to send (`duration` is divided over the tap
    ///                          events), defaults to 1
    ///     * `duration`: Duration of the event(s) in milliseconds, defaults to 300
    pub async fn tap(&self, args: Value) -> Result<ActionResult, Error> {
        fx_log_info!("Executing Tap in Input Facade.");
        let req: TapRequest = from_value(args)?;
        const DEFAULT_TAP_EVENT_COUNT: usize = 1;

        let width = match req.width {
            Some(x) => x,
            None => DEFAULT_DIMENSION,
        };
        let height = match req.height {
            Some(x) => x,
            None => DEFAULT_DIMENSION,
        };

        let tap_event_count = match req.tap_event_count {
            Some(x) => x,
            None => DEFAULT_TAP_EVENT_COUNT,
        };
        let duration = match req.duration {
            Some(x) => Duration::from_millis(x),
            None => Duration::from_millis(DEFAULT_DURATION),
        };

        input::tap_event_command(req.x, req.y, width, height, tap_event_count, duration).await?;
        Ok(ActionResult::Success)
    }

    /// Multi-Finger Taps for a touchscreen with default or custom
    /// width, height, duration, and tap event counts.
    ///
    /// # Arguments
    /// * `value`: will be parsed by MultiFingerTapRequest
    ///   * must include:
    ///     * `fingers`: List of FIDL struct `Touch` defined at
    ///                  sdk/fidl/fuchsia.ui.input/input_reports.fidl.
    ///   * optionally includes any of:
    ///     * `width`: Horizontal resolution of the touch panel, defaults to 1000
    ///     * `height`: Vertical resolution of the touch panel, defaults to 1000
    ///     * `tap_event_count`: Number of multi-finger tap events to send
    ///                          (`duration` is divided over the events), defaults to 1
    ///     * `duration`: Duration of the event(s) in milliseconds, defaults to 0
    ///
    /// Example:
    /// To send a 2-finger triple tap over 3s.
    /// multi_finger_tap(MultiFingerTap {
    ///   tap_event_count: 3,
    ///   duration: 3000,
    ///   fingers: [
    ///     Touch { finger_id: 1, x: 0, y: 0, width: 0, height: 0 },
    ///     Touch { finger_id: 2, x: 20, y: 20, width: 0, height: 0 },
    ///  ]
    /// });
    ///
    pub async fn multi_finger_tap(&self, args: Value) -> Result<ActionResult, Error> {
        fx_log_info!("Executing MultiFingerTap in Input Facade.");
        let req: MultiFingerTapRequest = from_value(args)?;
        const DEFAULT_TAP_EVENT_COUNT: usize = 1;

        let width = match req.width {
            Some(x) => x,
            None => DEFAULT_DIMENSION,
        };
        let height = match req.height {
            Some(x) => x,
            None => DEFAULT_DIMENSION,
        };

        let tap_event_count = match req.tap_event_count {
            Some(x) => x,
            None => DEFAULT_TAP_EVENT_COUNT,
        };
        let duration = match req.duration {
            Some(x) => Duration::from_millis(x),
            None => Duration::from_millis(DEFAULT_DURATION),
        };

        input::multi_finger_tap_event_command(
            req.fingers,
            width,
            height,
            tap_event_count,
            duration,
        )
        .await?;
        Ok(ActionResult::Success)
    }

    /// Swipe from coordinates (x0, y0) to (x1, y1) for a touchscreen with default
    /// or custom width, height, duration, and tap event counts
    ///
    /// # Arguments
    /// * `value`: will be parsed to SwipeRequest
    ///   * must include:
    ///     * `x0`: X axis start coordinate
    ///     * `y0`: Y axis start coordinate
    ///     * `x1`: X axis end coordinate
    ///     * `y1`: Y axis end coordinate
    ///   * optionally includes any of:
    ///     * `width`: Horizontal resolution of the touch panel, defaults to 1000
    ///     * `height`: Vertical resolution of the touch panel, defaults to 1000
    ///     * `tap_event_count`: Number of move events to send in between the down and up events of
    ///                          the swipe, defaults to `duration / 17` (to emulate a 60 HZ sensor)
    ///     * `duration`: Duration of the event(s) in milliseconds, default to 300
    pub async fn swipe(&self, args: Value) -> Result<ActionResult, Error> {
        fx_log_info!("Executing Swipe in Input Facade.");
        let req: SwipeRequest = from_value(args)?;

        let width = match req.width {
            Some(x) => x,
            None => DEFAULT_DIMENSION,
        };
        let height = match req.height {
            Some(x) => x,
            None => DEFAULT_DIMENSION,
        };

        let duration = match req.duration {
            Some(x) => Duration::from_millis(x),
            None => Duration::from_millis(DEFAULT_DURATION),
        };
        let tap_event_count = match req.tap_event_count {
            Some(x) => x,
            // 17 msec per move event, to emulate a ~60Hz sensor.
            None => duration.as_millis() as usize / 17,
        };

        input::swipe_command(
            req.x0,
            req.y0,
            req.x1,
            req.y1,
            height,
            width,
            tap_event_count,
            duration,
        )
        .await?;
        Ok(ActionResult::Success)
    }

    /// Swipes multiple fingers from start positions to end positions for a touchscreen.
    ///
    /// # Arguments
    /// * `value`: will be parsed to `MultiFingerSwipeRequest`
    ///   * must include:
    ///     * `fingers`: List of `FingerSwipe`s.
    ///       * All `x0` and `x1` values must be in the range (0, width), regardless of
    ///         whether the width is defaulted or explicitly specified.
    ///       * All `y0` and `y1` values must be in the range (0, height), regardless of
    ///         whether the height is defaulted or explicitly specified.
    ///   * optionally includes any of:
    ///     * `width`: Horizontal resolution of the touch panel, defaults to 1000
    ///     * `height`: Vertical resolution of the touch panel, defaults to 1000
    ///     * `move_event_count`: Number of move events to send in between the down and up events of
    ///        the swipe.
    ///        * Defaults to `duration / 17` (to emulate a 60 HZ sensor).
    ///        * If 0, only the down and up events will be sent.
    ///     * `duration`: Duration of the event(s) in milliseconds
    ///        * Defaults to 300 milliseconds.
    ///        * Must be large enough to allow for at least one nanosecond per move event.
    ///
    /// # Returns
    /// * `Ok(ActionResult::Success)` if the arguments were successfully parsed and events
    ///    successfully injected.
    /// * `Err(Error)` otherwise.
    ///
    /// # Example
    /// To send a two-finger swipe, with four events over two seconds:
    ///
    /// ```
    /// multi_finger_swipe(MultiFingerSwipeRequest {
    ///   fingers: [
    ///     FingerSwipe { x0: 0, y0:   0, x1: 100, y1:   0 },
    ///     FingerSwipe { x0: 0, y0: 100, x1: 100, y1: 100 },
    ///   ],
    ///   move_event_count: 4
    ///   duration: 2000,
    /// });
    /// ```
    pub async fn multi_finger_swipe(&self, args: Value) -> Result<ActionResult, Error> {
        fx_log_info!("Executing MultiFingerSwipe in Input Facade.");
        let req: MultiFingerSwipeRequest = from_value(args)?;

        let width = match req.width {
            Some(x) => x,
            None => DEFAULT_DIMENSION,
        };
        let height = match req.height {
            Some(x) => x,
            None => DEFAULT_DIMENSION,
        };

        let duration = match req.duration {
            Some(x) => Duration::from_millis(x),
            None => Duration::from_millis(DEFAULT_DURATION),
        };
        let move_event_count = match req.move_event_count {
            Some(x) => x,
            // 17 msec per move event, to emulate a ~60Hz sensor.
            None => duration.as_millis() as usize / 17,
        };
        ensure!(
            duration.as_nanos()
                >= u128::try_from(move_event_count)
                    .context("internal error while validating `duration`")?,
            "`duration` of {} nsec is too short for `move_event_count` of {}; \
            all events would have same timestamp",
            duration.as_nanos(),
            move_event_count
        );
        validate_fingers!(req.fingers, x0 <= width)?;
        validate_fingers!(req.fingers, x1 <= width)?;
        validate_fingers!(req.fingers, y0 <= height)?;
        validate_fingers!(req.fingers, y1 <= height)?;

        let start_fingers =
            req.fingers.iter().map(|finger| (finger.x0, finger.y0)).collect::<Vec<_>>();
        let end_fingers =
            req.fingers.iter().map(|finger| (finger.x1, finger.y1)).collect::<Vec<_>>();

        input::multi_finger_swipe_command(
            start_fingers,
            end_fingers,
            width,
            height,
            move_event_count,
            duration,
        )
        .await?;
        Ok(ActionResult::Success)
    }
}
