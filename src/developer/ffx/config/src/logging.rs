use {
    anyhow::{Context as _, Result},
    simplelog::{CombinedLogger, Config, ConfigBuilder, LevelFilter, SimpleLogger, WriteLogger},
    std::fs::{create_dir_all, File, OpenOptions},
    std::path::PathBuf,
};

const LOG_DIR: &str = "log.dir";
const LOG_ENABLED: &str = "log.enabled";

fn config() -> Config {
    // Sets the target level to "Error" so that all logs show their module
    // target in the logs.
    ConfigBuilder::new().set_target_level(LevelFilter::Error).build()
}

pub async fn log_file(name: &str) -> Result<std::fs::File> {
    let mut log_path: PathBuf = super::get(LOG_DIR).await?;
    create_dir_all(&log_path)?;
    log_path.push(format!("{}.log", name));
    OpenOptions::new()
        .write(true)
        .append(true)
        .create(true)
        .open(log_path)
        .context("opening log file")
}

pub async fn is_enabled() -> bool {
    super::get(LOG_ENABLED).await.unwrap_or(false)
}

pub async fn init(stdio: bool) -> Result<()> {
    let mut file: Option<File> = None;

    // XXX: The log file selection would ideally be moved up into the ffx startup where we
    // decide if we're in the daemon or not, which would enable us to cleanup this code path
    // and enable a "--verbose" flag to the frontend that both logs to stdio and to file.
    // Currently that is avoided here because stdio implies "don't log to this file".
    if is_enabled().await && !stdio {
        file = Some(log_file("ffx").await?);
    }

    CombinedLogger::init(get_loggers(stdio, file)).context("initializing logger")
}

fn get_loggers(stdio: bool, file: Option<File>) -> Vec<Box<dyn simplelog::SharedLogger>> {
    let mut loggers: Vec<Box<dyn simplelog::SharedLogger>> = vec![];

    // The daemon logs to stdio, and is redirected to file by spawn_daemon, which enables
    // panics and backtraces to also be included.
    if stdio {
        loggers.push(SimpleLogger::new(LevelFilter::Debug, config()));
    }

    if let Some(file) = file {
        loggers.push(WriteLogger::new(LevelFilter::Debug, config(), file));
    }

    loggers
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_get_loggers() {
        let loggers = get_loggers(false, None);
        assert!(loggers.len() == 0);

        // SimpleLogger (error logs to stderr, all other levels to stdout)
        let loggers = get_loggers(true, None);
        assert!(loggers.len() == 1);

        // WriteLogger (error logs to stderr, all other logs to file)
        let loggers = get_loggers(false, Some(tempfile::tempfile().unwrap()));
        assert!(loggers.len() == 1);

        // SimpleLogger & WriteLogger (error logs to stderr, all other levels to stdout and file)
        let loggers = get_loggers(true, Some(tempfile::tempfile().unwrap()));
        assert!(loggers.len() == 2);
    }
}
