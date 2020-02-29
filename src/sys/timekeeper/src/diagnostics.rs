use {
    anyhow::Context as _,
    fuchsia_inspect::{Inspector, IntProperty, Property},
    fuchsia_zircon as zx,
    futures::FutureExt,
    lazy_static::lazy_static,
};

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
    static ref START_TIME_MONO: IntProperty =
        INSPECTOR.root().create_int("start_time_monotonic_nanos", 0);
}

fn monotonic_time() -> i64 {
    zx::Time::get(zx::ClockId::Monotonic).into_nanos()
}

pub fn init() {
    fuchsia_syslog::init().context("initializing logging").unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);
    START_TIME_MONO.set(monotonic_time());
    INSPECTOR.root().record_lazy_child("current", || {
        async move {
            let inspector = Inspector::new();
            inspector.root().record_int("system_uptime_monotonic_nanos", monotonic_time());
            Ok(inspector)
        }
        .boxed()
    });
}
