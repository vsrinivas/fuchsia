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
    static ref UTC_UPDATED_METRIC: IntProperty = INSPECTOR.root().create_int("updated_at_mono", 0);
}

pub fn init() {
    fuchsia_syslog::init().context("initializing logging").unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);
    NETCLOCK_STARTED_METRIC.set(zx::Time::get(zx::ClockId::Monotonic).into_nanos());
}

pub fn utc_updated(at: i64) {
    log::info!("received UTC update notification, update generated at {}", at);
    UTC_UPDATED_METRIC.set(at);
}
