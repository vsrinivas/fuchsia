use {
    ffx_config::get,
    ffx_core::constants::{LOG_DIR, LOG_ENABLED},
    simplelog::{
        CombinedLogger, Config, ConfigBuilder, LevelFilter, TermLogger, TerminalMode, WriteLogger,
    },
    std::fs::OpenOptions,
};

fn debug_config() -> Config {
    ConfigBuilder::new()
        .set_target_level(LevelFilter::Info)
        .add_filter_ignore_str("tokio_reactor")
        .build()
}

async fn log_location(name: &str) -> String {
    let log_file = format!("{}.log", name);
    let mut log_dir = get!(str, LOG_DIR, "").await;
    if log_dir.len() > 0 && !log_dir.ends_with(std::path::MAIN_SEPARATOR) {
        log_dir = format!("{}{}", log_dir, std::path::MAIN_SEPARATOR);
    }
    format!("{}{}", log_dir, log_file)
}

async fn is_enabled() -> bool {
    get!(bool, LOG_ENABLED, false).await
}

pub(crate) async fn setup_logger(name: &str) {
    if !is_enabled().await {
        return;
    }
    let file = OpenOptions::new()
        .write(true)
        .append(true)
        .create(true)
        .open(log_location(name).await)
        .expect("could not open log file");
    CombinedLogger::init(vec![
        TermLogger::new(LevelFilter::Error, Config::default(), TerminalMode::Mixed)
            .expect("could not initialize error logger"),
        WriteLogger::new(LevelFilter::Debug, debug_config(), file),
    ])
    .expect("could not initialize logs");
    log::debug!("Logging configured");
}
