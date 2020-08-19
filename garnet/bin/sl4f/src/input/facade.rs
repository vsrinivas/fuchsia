use crate::input::types::{ActionResult, SwipeRequest, TapRequest};
use anyhow::Error;
use fuchsia_syslog::macros::fx_log_info;
use input_synthesis as input;
use serde_json::{from_value, Value};
use std::time::Duration;
const DEFAULT_DIMENSION: u32 = 1000;
const DEFAULT_DURATION: u64 = 300;

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
    /// * `x`: X axis coordinate
    /// * `y`: Y axis coordinate
    /// * `width`: Width of the display, default to 1000
    /// * `height`: Height of the display, default to 1000
    /// * `tap_event_count`: Number of tap events to send (`duration` is divided over the tap events), default to 1
    /// * `duration`: Duration of the event(s) in milliseconds, default to 300
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

    ///Swipe from coordinates (x0, y0) to (x1, y1) for a touchscreen with default
    ///or custom width, height, duration, and tap event counts
    ///
    /// # Arguments
    /// * `value`: will be parsed to SwipeRequest
    /// * `x0`: X axis start coordinate
    /// * `y0`: Y axis start coordinate
    /// * `x1`: X axis end coordinate
    /// * `y1`: Y axis end coordinate
    /// * `width`: Width of the display, default to 1000
    /// * `height`: Height of the display, default to 1000
    /// * `tap_event_count`: Number of move events to send in between the down and up events of the swipe, default to `duration` / 17
    /// * `duration`: Duration of the event(s) in milliseconds, default to 300
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
            // 17 move events per second to match ~60Hz sensor.
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
}
