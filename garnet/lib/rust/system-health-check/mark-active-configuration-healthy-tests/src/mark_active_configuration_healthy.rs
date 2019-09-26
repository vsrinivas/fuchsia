use {fuchsia_async as fasync, system_health_check::set_active_configuration_healthy};

#[fasync::run(1)]
async fn main() {
    set_active_configuration_healthy().await
}
