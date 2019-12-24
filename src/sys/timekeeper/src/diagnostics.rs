use {
    anyhow::Context as _,
    fuchsia_inspect::{Inspector, IntProperty, Property},
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
};

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
    static ref START_TIME_MONO: IntProperty = INSPECTOR.root().create_int("start_time_mono", 0);
}

pub fn init() {
    fuchsia_syslog::init().context("initializing logging").unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);
    START_TIME_MONO.set(zx::Time::get(zx::ClockId::Monotonic).into_nanos());
}
