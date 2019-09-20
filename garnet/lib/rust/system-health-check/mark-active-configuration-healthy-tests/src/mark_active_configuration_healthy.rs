use {fuchsia_async as fasync, system_health_check::mark_active_configuration_successful};

#[fasync::run(1)]
async fn main() {
    mark_active_configuration_successful().await
}
