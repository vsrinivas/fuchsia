pub mod constants;

pub use core_macros::{ffx_command, ffx_plugin};

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum ConfigLevel {
    Defaults,
    Build,
    Global,
    User,
}
