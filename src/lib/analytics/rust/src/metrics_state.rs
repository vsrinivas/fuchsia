// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Error, Result};
use std::fs::{create_dir_all, read_to_string, remove_file, File};
use std::io::Write;
use std::path::{Path, PathBuf};
use uuid::Uuid;

const OPT_IN_STATUS_FILENAME: &str = "analytics-status";

pub const UNKNOWN_APP_NAME: &str = "unknown_app";
pub const UNKNOWN_VERSION: &str = "unknown build version";
pub const UNKNOWN_PROPERTY_ID: &str = "unknown ga property id";

/// Maintains and memo-izes the operational state of the analytics service for the app.
#[derive(Debug, PartialEq)]
pub struct MetricsState {
    pub(crate) app_name: String,
    pub(crate) build_version: String,
    pub(crate) ga_product_code: String,
    pub(crate) status: MetricsStatus,
    pub(crate) uuid: Option<Uuid>,
    metrics_dir: PathBuf,
}

#[derive(Debug, PartialEq)]
pub(crate) enum MetricsStatus {
    Disabled,  // the environment is set to turn off analytics
    NewUser,   // user has never seen the full analytics notice for the Fuchsia tools
    NewToTool, // user has never seen the brief analytics notice for this tool
    OptedIn,   // user is allowing collection of analytics data
    OptedOut,  // user has opted out of collection of analytics data
}

impl MetricsState {
    pub(crate) fn from_config(
        metrics_dir: &PathBuf,
        app_name: String,
        build_version: String,
        ga_product_code: String,
        disabled: bool,
    ) -> Self {
        MetricsState::new(metrics_dir, app_name, build_version, ga_product_code, disabled)
    }

    pub(crate) fn new(
        metrics_dir: &PathBuf,
        app_name: String,
        build_version: String,
        ga_product_code: String,
        disabled: bool,
    ) -> MetricsState {
        let mut metrics = MetricsState::default();
        if disabled {
            metrics.status = MetricsStatus::Disabled;
            return metrics;
        }
        metrics.app_name = app_name;
        metrics.build_version = build_version;
        metrics.metrics_dir = PathBuf::from(metrics_dir);
        metrics.ga_product_code = ga_product_code;

        match read_opt_in_status(Path::new(&metrics_dir)) {
            Ok(true) => metrics.status = MetricsStatus::OptedIn,
            Ok(false) => metrics.status = MetricsStatus::OptedOut,
            Err(_) => {
                metrics.status = MetricsStatus::NewUser;
                if let Err(e) = write_opt_in_status(metrics_dir, true) {
                    eprintln!("Could not write opt in status {:}", e);
                }
            }
        }

        if metrics.status == MetricsStatus::OptedOut {
            return metrics;
        }

        match read_uuid_file(metrics_dir) {
            Ok(uuid) => metrics.uuid = Some(uuid),
            Err(_) => {
                let uuid = Uuid::new_v4();
                metrics.uuid = Some(uuid);

                if let Err(e) = write_uuid_file(metrics_dir, &uuid.to_string()) {
                    eprintln!("Could not write uuid file {:}", e);
                }
            }
        }

        if metrics.status == MetricsStatus::NewUser {
            // record usage of the app on disk, but, stay 'NewUser' to prevent collection on first usage.
            if let Err(e) = write_app_status(metrics_dir, &metrics.app_name, true) {
                eprintln!("Could not write app file,  {}, {:}", &metrics.app_name, e);
            }
            return metrics;
        }

        if let Err(_e) = read_app_status(metrics_dir, &metrics.app_name) {
            metrics.status = MetricsStatus::NewToTool;
            if let Err(e) = write_app_status(metrics_dir, &metrics.app_name, true) {
                eprintln!("Could not write app file,  {}, {:}", &metrics.app_name, e);
            }
        }

        metrics
    }

    pub(crate) fn set_opt_in_status(&mut self, opt_in: bool) -> Result<(), Error> {
        if self.status == MetricsStatus::Disabled {
            return Ok(());
        }

        match opt_in {
            true => self.status = MetricsStatus::OptedIn,
            false => self.status = MetricsStatus::OptedOut,
        }
        write_opt_in_status(&self.metrics_dir, opt_in)?;
        match self.status {
            MetricsStatus::OptedOut => {
                self.uuid = None;
                delete_uuid_file(&self.metrics_dir)?;
                delete_app_file(&self.metrics_dir, &self.app_name)?;
            }
            MetricsStatus::OptedIn => {
                let uuid = Uuid::new_v4();
                self.uuid = Some(uuid);

                write_uuid_file(&self.metrics_dir, &uuid.to_string())?;
                write_app_status(&self.metrics_dir, &self.app_name, true)?;
            }
            _ => (),
        };
        Ok(())
    }

    pub(crate) fn is_opted_in(&self) -> bool {
        match self.status {
            MetricsStatus::OptedIn | MetricsStatus::NewToTool => true,
            _ => false,
        }
    }

    // disable analytics for this invocation only
    // this does not affect the global analytics state
    pub fn opt_out_for_this_invocation(&mut self) -> Result<()> {
        if self.status == MetricsStatus::Disabled {
            return Ok(());
        }
        self.status = MetricsStatus::OptedOut;
        Ok(())
    }
}

impl Default for MetricsState {
    fn default() -> Self {
        MetricsState {
            app_name: String::from(UNKNOWN_APP_NAME),
            build_version: String::from(UNKNOWN_VERSION),
            ga_product_code: UNKNOWN_PROPERTY_ID.to_string(),
            status: MetricsStatus::NewUser,
            uuid: None,
            metrics_dir: PathBuf::from("/tmp"),
        }
    }
}

fn read_opt_in_status(metrics_dir: &Path) -> Result<bool, Error> {
    let status_file = metrics_dir.join(OPT_IN_STATUS_FILENAME);
    read_bool_from(&status_file)
}

pub(crate) fn write_opt_in_status(metrics_dir: &PathBuf, status: bool) -> Result<(), Error> {
    create_dir_all(&metrics_dir)?;

    let status_file = metrics_dir.join(OPT_IN_STATUS_FILENAME);
    write_bool_to(&status_file, status)
}

fn read_app_status(metrics_dir: &PathBuf, app: &str) -> Result<bool, Error> {
    let status_file = metrics_dir.join(app);
    read_bool_from(&status_file)
}

pub fn write_app_status(metrics_dir: &PathBuf, app: &str, status: bool) -> Result<(), Error> {
    create_dir_all(&metrics_dir)?;

    let status_file = metrics_dir.join(app);
    write_bool_to(&status_file, status)
}

fn read_bool_from(path: &PathBuf) -> Result<bool, Error> {
    let result = read_to_string(path)?;
    let parse = &result.trim_end().parse::<u8>()?;
    Ok(*parse != 0)
}

fn write_bool_to(status_file_path: &PathBuf, state: bool) -> Result<(), Error> {
    let tmp_file = File::create(status_file_path)?;
    writeln!(&tmp_file, "{}", state as u8)?;
    Ok(())
}

fn read_uuid_file(metrics_dir: &PathBuf) -> Result<Uuid, Error> {
    let file = metrics_dir.join("uuid");
    let path = file.as_path();
    let result = read_to_string(path)?;
    match Uuid::parse_str(result.trim_end()) {
        Ok(uuid) => Ok(uuid),
        Err(e) => Err(Error::from(e)),
    }
}

fn delete_uuid_file(metrics_dir: &PathBuf) -> Result<(), Error> {
    let file = metrics_dir.join("uuid");
    let path = file.as_path();
    Ok(remove_file(path)?)
}

fn delete_app_file(metrics_dir: &PathBuf, app: &str) -> Result<(), Error> {
    let status_file = metrics_dir.join(app);
    let path = status_file.as_path();
    Ok(remove_file(path)?)
}

fn write_uuid_file(dir: &PathBuf, uuid: &str) -> Result<(), Error> {
    create_dir_all(&dir)?;
    let file_obj = &dir.join(&"uuid");
    let uuid_file_path = file_obj.as_path();
    let uuid_file = File::create(uuid_file_path)?;
    writeln!(&uuid_file, "{}", uuid)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::metadata;
    use std::path::PathBuf;
    use tempfile::tempdir;

    const APP_NAME: &str = "ffx";
    const BUILD_VERSION: &str = "12/09/20 00:00:00";

    #[test]
    fn new_metrics() {
        let _m = MetricsState {
            app_name: String::from(APP_NAME),
            build_version: String::from(BUILD_VERSION),
            ga_product_code: UNKNOWN_PROPERTY_ID.to_string(),
            status: MetricsStatus::NewUser,
            uuid: Some(Uuid::new_v4()),
            metrics_dir: PathBuf::from("/tmp"),
        };
    }

    #[test]
    fn new_user_of_any_tool() -> Result<(), Error> {
        let dir = create_tmp_metrics_dir()?;
        let m = MetricsState::new(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );
        assert_eq!(m.status, MetricsStatus::NewUser);
        let result = read_uuid_file(&dir);
        match result {
            Ok(uuid) => {
                assert_eq!(m.uuid, Some(uuid));
            }
            Err(_) => panic!("Could not read uuid"),
        }

        drop(dir);
        Ok(())
    }

    #[test]
    fn existing_user_first_use_of_this_tool() -> Result<(), Error> {
        let dir = create_tmp_metrics_dir()?;
        write_opt_in_status(&dir, true)?;

        let uuid = Uuid::default();
        write_uuid_file(&dir, &uuid.to_string())?;

        let m = MetricsState::new(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_ne!(Some(&m), None);
        assert_eq!(m.status, MetricsStatus::NewToTool);
        assert_eq!(m.uuid, Some(uuid));
        let app_status_file = &dir.join(&APP_NAME);
        assert!(metadata(app_status_file).is_ok(), "App status file should exist.");

        drop(dir);
        Ok(())
    }

    #[test]
    fn existing_user_of_this_tool_opted_in() -> Result<(), Error> {
        let dir = create_tmp_metrics_dir()?;
        write_opt_in_status(&dir, true)?;
        write_app_status(&dir, APP_NAME, true)?;
        let uuid = Uuid::default();
        write_uuid_file(&dir, &uuid.to_string())?;

        let m = MetricsState::new(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_ne!(Some(&m), None);
        assert_eq!(m.status, MetricsStatus::OptedIn);
        assert_eq!(m.uuid, Some(uuid));

        drop(dir);
        Ok(())
    }

    #[test]
    fn existing_user_of_this_tool_opted_out() -> Result<(), Error> {
        let dir = create_tmp_metrics_dir()?;
        write_opt_in_status(&dir, false)?;
        write_app_status(&dir, &APP_NAME, true)?;

        let m = MetricsState::new(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_ne!(Some(&m), None);
        assert_eq!(m.status, MetricsStatus::OptedOut);
        assert_eq!(m.uuid, None);

        drop(dir);
        Ok(())
    }

    #[test]
    fn with_disable_env_var_set() -> Result<(), Error> {
        let dir = create_tmp_metrics_dir()?;
        let m = MetricsState::new(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            true,
        );

        assert_eq!(m.status, MetricsStatus::Disabled);
        assert_eq!(m.uuid, None);
        Ok(())
    }

    #[test]
    fn existing_user_of_this_tool_opted_in_then_out_then_in() -> Result<(), Error> {
        let dir = create_tmp_metrics_dir()?;
        write_opt_in_status(&dir, true)?;
        write_app_status(&dir, &APP_NAME, true)?;
        let uuid = Uuid::default();
        write_uuid_file(&dir, &uuid.to_string())?;
        let mut m = MetricsState::new(
            &dir,
            String::from(APP_NAME),
            String::from(BUILD_VERSION),
            UNKNOWN_PROPERTY_ID.to_string(),
            false,
        );

        assert_ne!(Some(&m), None);
        assert_eq!(m.status, MetricsStatus::OptedIn);
        assert_eq!(m.uuid, Some(uuid));

        &m.set_opt_in_status(false)?;

        assert_eq!(m.status, MetricsStatus::OptedOut);
        assert_eq!(m.uuid, None);
        let app_status_file = &dir.join(&APP_NAME);
        assert!(metadata(app_status_file).is_err(), "App status file should not exist.");

        &m.set_opt_in_status(true)?;

        assert_eq!(m.status, MetricsStatus::OptedIn);
        assert_eq!(m.uuid, Some(read_uuid_file(&dir).unwrap()));
        assert_eq!(true, read_app_status(&dir, &APP_NAME)?);

        drop(dir);
        Ok(())
    }

    pub fn create_tmp_metrics_dir() -> Result<PathBuf, Error> {
        let tmp_dir = tempdir()?;
        let dir_obj = tmp_dir.path().join("fuchsia_metrics");
        let dir = dir_obj.as_path();
        Ok(dir.to_owned())
    }
}
