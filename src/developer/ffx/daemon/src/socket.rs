// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Result};
use serde::{Serialize, Deserialize};
use std::fmt::Display;
use std::path::PathBuf;

#[derive(Debug, Serialize, Deserialize)]
enum SocketStatus {
    NotPresent,
    Present,
}

impl Display for SocketStatus {
    fn fmt(&self, out: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        use SocketStatus::*;
        write!(out, "{}", match self {
            NotPresent => "Not Present",
            Present => "Present",
        })
    }
}

#[derive(Debug, Serialize, Deserialize)]
enum PidStatus {
    NotPresent,
    NotRunning { pid: Option<u32> },
    Running { pid: u32 },
}

impl Display for PidStatus {
    fn fmt(&self, out: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        use PidStatus::*;
        write!(out, "{}", match self {
            NotPresent => "Not Present".to_owned(),
            NotRunning { pid: Some(pid) } => format!("Present, but not running (last pid: {pid})"),
            NotRunning { pid: None } => format!("Present, but not valid"),
            Running { pid } => format!("Present and running (pid: {pid}"),
        })
    }
}

#[derive(Debug, Serialize, Deserialize)]
struct FileInfo<T> {
    path: PathBuf,
    status: T,
}

impl<T> FileInfo<T> {
    fn new(path: PathBuf, status: T) -> Self {
        Self { path, status }
    }
}

impl<T: Display> Display for FileInfo<T> {
    fn fmt(&self, out: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        writeln!(out, "   Path: {}", self.path.display())?;
        writeln!(out, "   Status: {}", self.status)?;
        Ok(())
    }
}

/// Loads details about a daemon socket file and its associated pid file.
#[derive(Debug, Serialize, Deserialize)]
pub struct SocketDetails {
    socket: FileInfo<SocketStatus>,
    pid: FileInfo<PidStatus>,
}

impl Display for SocketDetails {
    fn fmt(&self, out: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        writeln!(out, "Socket:")?;
        write!(out, "{}", self.socket)?;
        writeln!(out, "")?;
        writeln!(out, "Pid:")?;
        write!(out, "{}", self.pid)?;
        Ok(())
    }
}

impl SocketDetails {
    /// Loads information about the socket based on the given path
    pub fn new(socket_path: PathBuf) -> Self {
        let pid_path = socket_path.with_extension("pid");

        // ignore errors trying to read this and just treat them as "no pid"
        let pid = std::fs::File::open(&pid_path).ok().and_then(|pid_file| serde_json::from_reader(pid_file).ok());
        let pid_running = pid.and_then(check_if_running);

        let socket_status = match socket_path.exists() {
            true => SocketStatus::Present,
            false => SocketStatus::NotPresent,
        };
        let pid_status = match (pid_path.exists(), pid_running) {
            (true, Some(pid)) => PidStatus::Running { pid },
            (true, None) => PidStatus::NotRunning { pid },
            (false, _) => PidStatus::NotPresent,
        };

        let socket = FileInfo::new(socket_path, socket_status);
        let pid = FileInfo::new(pid_path, pid_status);

        SocketDetails { socket, pid }
    }
}

fn check_if_running(pid: u32) -> Option<u32> {
    let nix_pid = nix::unistd::Pid::from_raw(pid.try_into().ok()?);
    nix::sys::signal::kill(nix_pid, None).ok()?;
    Some(pid)
}