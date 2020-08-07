use {
    crate::constants::{LOG_DIR, LOG_ENABLED},
    ffx_config::get,
    simplelog::{
        CombinedLogger, Config, ConfigBuilder, LevelFilter, TermLogger, TerminalMode, WriteLogger,
    },
    std::fs::OpenOptions,
    std::path::{Path, PathBuf},
};

fn debug_config() -> Config {
    // Sets the target level to "Error" so that all logs show their module
    // target in the logs.
    ConfigBuilder::new().set_target_level(LevelFilter::Error).build()
}

async fn log_location(name: &str) -> PathBuf {
    let log_file = format!("{}.log", name);
    let log_dir = get!(str, LOG_DIR, "").await;
    Path::new(&log_dir).join::<_>(log_file)
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
