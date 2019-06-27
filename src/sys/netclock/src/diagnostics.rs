use {
    failure::ResultExt,
    fuchsia_inspect::{Inspector, IntProperty, Property},
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
};

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
    static ref NETCLOCK_STARTED_METRIC: IntProperty =
        INSPECTOR.root().create_int("start_time_mono", 0);
}

pub fn init() {
    fuchsia_syslog::init().context("initializing logging").unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);
    NETCLOCK_STARTED_METRIC.set(zx::Time::get(zx::ClockId::Monotonic).into_nanos());
}
