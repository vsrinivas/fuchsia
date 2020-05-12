pub mod constants;

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum ConfigLevel {
    Defaults,
    Build,
    Global,
    User,
}
