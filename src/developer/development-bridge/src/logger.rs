use {
    crate::config::{get_config_bool, get_config_str},
    crate::constants::{LOG_DIR, LOG_ENABLED},
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

fn log_location(name: &str) -> String {
    let log_file = format!("{}.log", name);
    let mut log_dir = get_config_str(LOG_DIR, "");
    if log_dir.len() > 0 && !log_dir.ends_with(std::path::MAIN_SEPARATOR) {
        log_dir = format!("{}{}", log_dir, std::path::MAIN_SEPARATOR);
    }
    format!("{}{}", log_dir, log_file)
}

fn is_enabled() -> bool {
    get_config_bool(LOG_ENABLED, false)
}

pub(crate) fn setup_logger(name: &str) {
    if !is_enabled() {
        return;
    }
    let file = OpenOptions::new()
        .write(true)
        .append(true)
        .create(true)
        .open(log_location(name))
        .expect("could not open log file");
    CombinedLogger::init(vec![
        TermLogger::new(LevelFilter::Error, Config::default(), TerminalMode::Mixed)
            .expect("could not initialize error logger"),
        WriteLogger::new(LevelFilter::Debug, debug_config(), file),
    ])
    .expect("could not initialize logs");
    log::debug!("Logging configured");
}
