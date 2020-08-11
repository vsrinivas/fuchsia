// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fuchsia_inspect_contrib::reader::{ArchiveReader, Inspect, NodeHierarchy, Property},
    fuchsia_zircon as zx,
    std::{cmp::Reverse, collections::HashMap, fmt, str::FromStr},
};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LogSeverity {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
}

impl FromStr for LogSeverity {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self, Error> {
        match s.to_lowercase().as_str() {
            "fatal" => Ok(LogSeverity::FATAL),
            "error" => Ok(LogSeverity::ERROR),
            "warn" | "warning" => Ok(LogSeverity::WARN),
            "info" => Ok(LogSeverity::INFO),
            "debug" => Ok(LogSeverity::DEBUG),
            "trace" => Ok(LogSeverity::TRACE),
            _ => Err(format_err!("{} is not a valid log severity", s)),
        }
    }
}

// Number of log messages broken down by severity for a given component.
pub struct ComponentLogStats {
    component_url: String,
    short_name: String,
    start_time: i64,
    log_counts: Vec<u64>,
    total_logs: u64,
}

impl ComponentLogStats {
    pub fn new(node: &NodeHierarchy, start_times: &HashMap<String, f64>) -> ComponentLogStats {
        let component_url = node.name.clone();
        let short_name = {
            let last_slash_index = component_url.rfind("/");
            match last_slash_index {
                Some(index) => &component_url[index + 1..],
                None => component_url.as_str(),
            }
            .to_string()
        };
        let start_time = match start_times.get(&short_name) {
            Some(s) => *s as i64,
            None => 0,
        };
        let map = node
            .properties
            .iter()
            .map(|x| match x {
                Property::Int(name, value) => (name.as_str(), *value as u64),
                Property::Uint(name, value) => (name.as_str(), *value),
                _ => ("", 0),
            })
            .collect::<HashMap<_, _>>();
        ComponentLogStats {
            component_url,
            short_name,
            start_time,
            log_counts: vec![
                map["trace_logs"],
                map["debug_logs"],
                map["info_logs"],
                map["warning_logs"],
                map["error_logs"],
                map["fatal_logs"],
            ],
            total_logs: map["total_logs"],
        }
    }

    pub fn error_rate(&self, now: i64) -> f64 {
        let uptime_in_hours = (now - self.start_time) as f64 / 3600.0;
        self.get_count(LogSeverity::ERROR) as f64 / uptime_in_hours
    }

    pub fn get_count(&self, severity: LogSeverity) -> u64 {
        self.log_counts[severity as usize]
    }

    fn get_sort_key(&self) -> (u64, u64, u64, u64, u64) {
        (
            // Fatal logs are reported separately. They shouldn't affect the order of the output.
            self.get_count(LogSeverity::ERROR),
            self.get_count(LogSeverity::WARN),
            self.get_count(LogSeverity::INFO),
            self.get_count(LogSeverity::DEBUG),
            self.get_count(LogSeverity::TRACE),
        )
    }
}

/// Log statistics broken down by component.
pub struct LogStats {
    /// The list of log statistics broken down by component.
    stats_list: Vec<ComponentLogStats>,
    /// The minimum severity log to display.
    min_severity: LogSeverity,
}

impl LogStats {
    pub async fn new(min_severity: LogSeverity) -> Result<Self, Error> {
        let mut response = ArchiveReader::new()
            .add_selector("core/archivist:root/log_stats/by_component/*:*")
            .add_selector("core/archivist:root/event_stats/recent_events/*:*")
            .snapshot::<Inspect>()
            .await?;

        if response.len() != 1 {
            return Err(format_err!("Expected one inspect tree, received {}", response.len()));
        }

        let inspect_root = response.pop().and_then(|r| r.payload).unwrap();

        Self::new_with_root(min_severity, &inspect_root).await
    }

    pub async fn new_with_root(
        min_severity: LogSeverity,
        inspect_root: &NodeHierarchy,
    ) -> Result<Self, Error> {
        let start_times = Self::extract_component_start_times(&inspect_root)?;

        let stats_node = inspect_root
            .get_child_by_path(&vec!["log_stats", "by_component"])
            .ok_or(format_err!("Missing log_stats/by_component"))?;
        let mut stats_list = stats_node
            .children
            .iter()
            .map(|x| ComponentLogStats::new(x, &start_times))
            .collect::<Vec<_>>();
        stats_list.sort_by_key(|x| Reverse(x.get_sort_key()));

        Ok(Self { stats_list, min_severity })
    }

    pub fn min_severity(&self) -> LogSeverity {
        //  Min severity cannot be FATAL.
        if self.min_severity == LogSeverity::FATAL {
            LogSeverity::ERROR
        } else {
            self.min_severity.clone()
        }
    }

    pub fn get_by_url(&self, url: &str) -> Option<&ComponentLogStats> {
        self.stats_list.iter().filter(|x| x.component_url == url).next()
    }

    // Extracts the component start times from the inspect hierarchy.
    fn extract_component_start_times(
        inspect_root: &NodeHierarchy,
    ) -> Result<HashMap<String, f64>, Error> {
        let mut res = HashMap::new();
        let events_node = inspect_root
            .get_child_by_path(&vec!["event_stats", "recent_events"])
            .ok_or(format_err!("Missing event_stats/recent_events"))?;

        for event in &events_node.children {
            // Extract the component name from the moniker. This allows us to match against the
            // component url from log stats.
            let moniker = event
                .get_property("moniker")
                .and_then(|prop| prop.string())
                .ok_or(format_err!("Missing moniker"))?;
            let last_slash_index = moniker.rfind("/");
            if let Some(i) = last_slash_index {
                let last_colon_index = moniker.rfind(":");
                if let Some(j) = last_colon_index {
                    let time = event
                        .get_property("@time")
                        .and_then(|prop| prop.string())
                        .ok_or(format_err!("Missing @time"))?;
                    res.insert(moniker[i + 1..j].into(), time.parse::<f64>()?);
                }
            }
        }

        Ok(res)
    }
}

impl fmt::Display for LogStats {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut output_str = String::new();
        // Number of fatal logs is expected to be zero. If that's not the case, report it here.
        for stats in &self.stats_list {
            if stats.get_count(LogSeverity::FATAL) != 0 {
                output_str.push_str(&format!(
                    "Found {} fatal log messages for component {}\n",
                    stats.get_count(LogSeverity::FATAL),
                    stats.component_url
                ));
            }
        }

        let min_severity_int = self.min_severity() as usize;
        let max_severity_int = LogSeverity::ERROR as usize;

        let severity_strs = vec!["TRACE", "DEBUG", "INFO", "WARN", "ERROR"];
        for i in (min_severity_int..=max_severity_int).rev() {
            output_str.push_str(&format!("{:<7}", severity_strs[i]));
        }
        output_str.push_str(&format!("{:<7}{:<10}{}\n", "Total", "ERROR/h", "Component"));

        let now = zx::Time::get(zx::ClockId::Monotonic).into_nanos() / 1_000_000_000;

        for stats in &self.stats_list {
            for i in (min_severity_int..=max_severity_int).rev() {
                output_str.push_str(&format!("{:<7}", stats.log_counts[i]));
            }
            output_str.push_str(&format!("{:<7}", stats.total_logs));
            output_str.push_str(&format!("{:<10.4}", stats.error_rate(now)));
            output_str.push_str(&format!("{}\n", stats.short_name));
        }

        write!(f, "{}", output_str)
    }
}
