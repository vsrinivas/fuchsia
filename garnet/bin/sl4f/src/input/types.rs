use serde_derive::{Deserialize, Serialize};

/// Enum for supported Input commands.
pub enum InputMethod {
    Tap,
    Swipe,
}

impl std::str::FromStr for InputMethod {
    type Err = anyhow::Error;
    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Tap" => Ok(InputMethod::Tap),
            "Swipe" => Ok(InputMethod::Swipe),
            _ => return Err(format_err!("Invalid Input Facade method: {}", method)),
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct TapRequest {
    pub x: u32,
    pub y: u32,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub tap_event_count: Option<usize>,
    pub duration: Option<u64>,
}

#[derive(Deserialize, Debug)]
pub struct SwipeRequest {
    pub x0: u32,
    pub y0: u32,
    pub x1: u32,
    pub y1: u32,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub tap_event_count: Option<usize>,
    pub duration: Option<u64>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum ActionResult {
    Success,
    Fail,
}
