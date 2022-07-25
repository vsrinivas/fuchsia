// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    diagnostics_data::{LogsData, Timestamp},
    serde::{
        de::{self as de, Error},
        Deserialize, Serialize,
    },
    std::time::SystemTime,
};

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum EventType {
    LoggingStarted,
    TargetDisconnected,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum LogData {
    #[serde(deserialize_with = "deserialize_target_log")]
    TargetLog(LogsData),
    #[serde(deserialize_with = "deserialize_symbolized_target_log")]
    SymbolizedTargetLog(LogsData, String),
    MalformedTargetLog(String),
    FfxEvent(EventType),
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct LogEntry {
    pub data: LogData,
    pub timestamp: Timestamp,
    pub version: u64,
}

impl LogEntry {
    pub fn new(data: LogData) -> Result<Self> {
        Ok(LogEntry {
            data: data,
            version: 1,
            timestamp: Timestamp::from(
                SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .context("system time before Unix epoch")?
                    .as_nanos() as i64,
            ),
        })
    }
}

impl From<LogsData> for LogData {
    fn from(data: LogsData) -> Self {
        Self::TargetLog(data)
    }
}

fn parse_log_data(value: serde_json::Value) -> Result<LogsData, serde_json::Error> {
    serde_json::from_value(value.clone())
}

fn deserialize_target_log<'de, D>(deserializer: D) -> Result<LogsData, D::Error>
where
    D: de::Deserializer<'de>,
{
    struct TargetLogVisitor;

    impl<'de> de::Visitor<'de> for TargetLogVisitor {
        type Value = LogsData;

        fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            formatter.write_str("a JSON map of LogsData")
        }

        fn visit_map<M>(self, v: M) -> Result<Self::Value, M::Error>
        where
            M: de::MapAccess<'de>,
        {
            let value: serde_json::Value =
                serde_json::Value::deserialize(de::value::MapAccessDeserializer::new(v))?;
            parse_log_data(value).map_err(|e| M::Error::custom(e.to_string()))
        }
    }

    deserializer.deserialize_any(TargetLogVisitor)
}
fn deserialize_symbolized_target_log<'de, D>(
    deserializer: D,
) -> Result<(LogsData, String), D::Error>
where
    D: de::Deserializer<'de>,
{
    struct SymbolizedTargetLogVisitor;

    impl<'de> de::Visitor<'de> for SymbolizedTargetLogVisitor {
        type Value = (LogsData, String);

        fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            formatter.write_str("a tuple of (LogsData, String)")
        }

        fn visit_seq<M>(self, v: M) -> Result<Self::Value, M::Error>
        where
            M: de::SeqAccess<'de>,
        {
            let raw_value: serde_json::Value =
                serde_json::Value::deserialize(de::value::SeqAccessDeserializer::new(v))?;

            let value: (serde_json::Value, String) =
                serde_json::from_value(raw_value).map_err(|e| M::Error::custom(e.to_string()))?;
            let data = parse_log_data(value.0).map_err(|e| M::Error::custom(e.to_string()))?;
            Ok((data, value.1))
        }
    }

    deserializer.deserialize_any(SymbolizedTargetLogVisitor)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        diagnostics_data::{BuilderArgs, LogsDataBuilder, Severity},
    };

    const LOG_ENTRY: &str = r#"
    {
        "data": {
          "TargetLog": {
            "data_source": "Logs",
            "metadata": {
              "errors": null,
              "component_url": "fuchsia-pkg://fuchsia.com/network#meta/netstack.cm",
              "timestamp": 263002243373398,
              "severity": "WARN",
              "tags": [
                "netstack",
                "DHCP"
              ],
              "pid": 25105,
              "tid": 0,
              "file": null,
              "line": null,
              "dropped": 0
            },
            "moniker": "core/network/netstack",
            "payload": {
              "root": {
                "message": {
                  "value": "netstack message"
                }
              }
            },
            "version": 1
          }
        },
        "timestamp": 1623435958093957000,
        "version": 1
      }    "#;

    const SYMBOLIZED_LOG_ENTRY: &str = r#"
    {
        "data": {
          "SymbolizedTargetLog": [{
            "data_source": "Logs",
            "metadata": {
              "errors": null,
              "component_url": "fuchsia-pkg://fuchsia.com/network#meta/netstack.cm",
              "timestamp": 263002243373398,
              "severity": "WARN",
              "tags": [
                "netstack",
                "DHCP"
              ],
              "pid": 25105,
              "tid": 0,
              "file": null,
              "line": null,
              "dropped": 0
            },
            "moniker": "core/network/netstack",
            "payload": {
              "root": {
                "message": {
                  "value": "netstack message"
                }
              }
            },
            "version": 1
          }, "symbolized"]
        },
        "timestamp": 1623435958093957000,
        "version": 1
      }    "#;

    fn symbolized_entry() -> LogEntry {
        LogEntry {
            data: LogData::SymbolizedTargetLog(expected_log_data(), "symbolized".to_string()),
            version: 1,
            timestamp: Timestamp::from(1623435958093957000i64),
        }
    }

    fn expected_log_data() -> LogsData {
        LogsDataBuilder::new(BuilderArgs {
            moniker: String::from("core/network/netstack"),
            timestamp_nanos: Timestamp::from(263002243373398i64),
            component_url: Some(String::from("fuchsia-pkg://fuchsia.com/network#meta/netstack.cm")),
            severity: Severity::Warn,
        })
        .set_message(String::from("netstack message"))
        .set_tid(0)
        .set_pid(25105)
        .add_tag(String::from("netstack"))
        .add_tag(String::from("DHCP"))
        .build()
    }
    fn expected_log_entry() -> LogEntry {
        LogEntry {
            data: LogData::TargetLog(expected_log_data()),
            version: 1,
            timestamp: Timestamp::from(1623435958093957000i64),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test() {
        let entry: LogEntry = serde_json::from_str(LOG_ENTRY).unwrap();
        assert_eq!(entry, expected_log_entry());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_symbolized() {
        let entry: LogEntry = serde_json::from_str(SYMBOLIZED_LOG_ENTRY).unwrap();
        assert_eq!(entry, symbolized_entry());
    }
}
