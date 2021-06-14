// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_config::get,
    ffx_core::ffx_plugin,
    ffx_daemon_log_args::LogCommand,
    std::fs::File,
    std::io::{BufRead, BufReader},
    std::path::PathBuf,
};

fn get_data(reader: BufReader<File>, cmd: LogCommand) -> Vec<String> {
    let lines: Vec<_> = reader.lines().map(|line| line.unwrap()).collect();
    match cmd.line_count {
        None => lines,
        Some(line_count) => lines.into_iter().rev().take(line_count).rev().collect(),
    }
}

#[ffx_plugin()]
pub async fn daemon(cmd: LogCommand) -> Result<()> {
    if !get("log.enabled").await? {
        ffx_bail!("Logging is not enabled.");
    }
    let mut log_path: PathBuf = get("log.dir").await?;
    log_path.push("ffx.daemon.log");
    let file = File::open(log_path).or_else(|_| ffx_bail!("Daemon log not found."))?;
    let reader = BufReader::new(file);

    for line in get_data(reader, cmd).iter() {
        println!("{}", line);
    }

    Ok(())
}

#[cfg(test)]
mod test {
    use {super::*, std::io::Write};

    fn write_log(dir: &tempfile::TempDir, lines: std::ops::Range<usize>) -> PathBuf {
        let log_path = dir.path().join("log.txt");
        let mut file = File::create(&log_path).unwrap();
        for line_idx in lines {
            writeln!(file, "line {}", line_idx).unwrap();
        }
        log_path
    }

    fn data_output(log_path: PathBuf, cmd: LogCommand) -> Vec<String> {
        let file = File::open(log_path).unwrap();
        let reader = BufReader::new(file);
        get_data(reader, cmd)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn display_x_most_recent_from_many_logs() {
        let dir = tempfile::tempdir().unwrap();
        let log_path = write_log(&dir, 1usize..20);
        let data = data_output(log_path, LogCommand { line_count: Some(10) });

        let expected: Vec<_> = (10u8..20).map(|idx| format!("line {}", idx)).collect();
        assert_eq!(data, expected);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn display_x_most_recent_from_not_enough_logs() {
        let dir = tempfile::tempdir().unwrap();
        let log_path = write_log(&dir, 1usize..5);
        let data = data_output(log_path, LogCommand { line_count: Some(10) });

        let expected: Vec<_> = (1u8..5).map(|idx| format!("line {}", idx)).collect();
        assert_eq!(data, expected);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn display_all_logs() {
        let dir = tempfile::tempdir().unwrap();
        let log_path = write_log(&dir, 1usize..20);
        let data = data_output(log_path, LogCommand { line_count: None });

        let expected: Vec<_> = (1u8..20).map(|idx| format!("line {}", idx)).collect();
        assert_eq!(data, expected);
    }
}
