// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    diagnostics_data::{BuilderArgs, LogsData, LogsDataBuilder, Timestamp},
    logs_data_v1::{
        LogsData as LogsDataV1, LogsField as LogsFieldV1, LogsProperty as LogsPropertyV1,
    },
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

fn v1_to_v2(data: LogsDataV1) -> LogsData {
    let mut builder = LogsDataBuilder::new(BuilderArgs {
        moniker: data.moniker.clone(),
        timestamp_nanos: data.metadata.timestamp.clone(),
        component_url: data.metadata.component_url.clone(),
        severity: data.metadata.severity.clone(),
        size_bytes: data.metadata.size_bytes.clone(),
    })
    .set_message(data.msg().unwrap_or("").to_string())
    .set_tid(data.tid().unwrap_or(0))
    .set_pid(data.pid().unwrap_or(0));

    if let Some(tags) = data.payload.as_ref().map(|p| {
        p.properties.iter().filter_map(|property| match property {
            LogsPropertyV1::String(LogsFieldV1::Tag, tag) => Some(tag.clone()),
            _ => None,
        })
    }) {
        for tag in tags {
            builder = builder.add_tag(tag);
        }
    }
    builder.build()
}
fn parse_log_data(value: serde_json::Value) -> Result<LogsData, serde_json::Error> {
    let first_pass: Result<LogsData, _> = serde_json::from_value(value.clone());

    if let Ok(data) = first_pass {
        if data.msg().is_some() {
            return Ok(data);
        }
    }

    let second_pass: LogsDataV1 = serde_json::from_value(value)?;
    Ok(v1_to_v2(second_pass))
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
    use {super::*, diagnostics_data::Severity};

    const V1_LOG_ENTRY: &str = r#"
{
    "data": {
      "TargetLog": {
        "data_source": "Logs",
        "metadata": {
          "errors": null,
          "component_url": "fuchsia-pkg://fuchsia.com/network#meta/netstack.cm",
          "timestamp": 403649538626725,
          "severity": "Info",
          "size_bytes": 158
        },
        "moniker": "core/network/netstack",
        "payload": {
          "root": {
            "pid": 17247,
            "tag": "DHCP",
            "tid": 0,
            "message": "netstack message"
          }
        },
        "version": 1
      }
    },
    "timestamp": 1620691752175298600,
    "version": 1
  }
    "#;

    const V1_SYMBOLIZED_LOG_ENTRY: &str = r#"
{
    "data": {
      "SymbolizedTargetLog": [{
        "data_source": "Logs",
        "metadata": {
          "errors": null,
          "component_url": "fuchsia-pkg://fuchsia.com/network#meta/netstack.cm",
          "timestamp": 403649538626725,
          "severity": "INFO",
          "size_bytes": 158
        },
        "moniker": "core/network/netstack",
        "payload": {
          "root": {
            "pid": 17247,
            "tag": "DHCP",
            "tid": 0,
            "message": "netstack message"
          }
        },
        "version": 1
      }, "symbolized"]
    },
    "timestamp": 1620691752175298600,
    "version": 1
  }"#;

    const V2_LOG_ENTRY: &str = r#"
    {
        "data": {
          "TargetLog": {
            "data_source": "Logs",
            "metadata": {
              "errors": null,
              "component_url": "fuchsia-pkg://fuchsia.com/network#meta/netstack.cm",
              "timestamp": 263002243373398,
              "severity": "WARN",
              "size_bytes": 137,
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

    const V2_SYMBOLIZED_LOG_ENTRY: &str = r#"
    {
        "data": {
          "SymbolizedTargetLog": [{
            "data_source": "Logs",
            "metadata": {
              "errors": null,
              "component_url": "fuchsia-pkg://fuchsia.com/network#meta/netstack.cm",
              "timestamp": 263002243373398,
              "severity": "WARN",
              "size_bytes": 137,
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

    fn symbolized_v1_entry() -> LogEntry {
        LogEntry {
            data: LogData::SymbolizedTargetLog(expected_v1_log_data(), "symbolized".to_string()),
            version: 1,
            timestamp: Timestamp::from(1620691752175298600i64),
        }
    }
    fn symbolized_v2_entry() -> LogEntry {
        LogEntry {
            data: LogData::SymbolizedTargetLog(expected_v2_log_data(), "symbolized".to_string()),
            version: 1,
            timestamp: Timestamp::from(1623435958093957000i64),
        }
    }

    fn expected_v1_log_data() -> LogsData {
        LogsDataBuilder::new(BuilderArgs {
            moniker: String::from("core/network/netstack"),
            timestamp_nanos: Timestamp::from(403649538626725i64),
            component_url: Some(String::from("fuchsia-pkg://fuchsia.com/network#meta/netstack.cm")),
            severity: Severity::Info,
            size_bytes: 158,
        })
        .set_message(String::from("netstack message"))
        .set_tid(0)
        .set_pid(17247)
        .add_tag(String::from("DHCP"))
        .build()
    }

    fn expected_v1_log_entry() -> LogEntry {
        LogEntry {
            data: LogData::TargetLog(expected_v1_log_data()),
            version: 1,
            timestamp: Timestamp::from(1620691752175298600i64),
        }
    }
    fn expected_v2_log_data() -> LogsData {
        LogsDataBuilder::new(BuilderArgs {
            moniker: String::from("core/network/netstack"),
            timestamp_nanos: Timestamp::from(263002243373398i64),
            component_url: Some(String::from("fuchsia-pkg://fuchsia.com/network#meta/netstack.cm")),
            severity: Severity::Warn,
            size_bytes: 137,
        })
        .set_message(String::from("netstack message"))
        .set_tid(0)
        .set_pid(25105)
        .add_tag(String::from("netstack"))
        .add_tag(String::from("DHCP"))
        .build()
    }
    fn expected_v2_log_entry() -> LogEntry {
        LogEntry {
            data: LogData::TargetLog(expected_v2_log_data()),
            version: 1,
            timestamp: Timestamp::from(1623435958093957000i64),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_v1() {
        let entry: LogEntry = serde_json::from_str(V1_LOG_ENTRY).unwrap();
        assert_eq!(entry, expected_v1_log_entry(),);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_v1_symbolized() {
        let entry: LogEntry = serde_json::from_str(V1_SYMBOLIZED_LOG_ENTRY).unwrap();
        assert_eq!(entry, symbolized_v1_entry());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_v2() {
        let entry: LogEntry = serde_json::from_str(V2_LOG_ENTRY).unwrap();
        assert_eq!(entry, expected_v2_log_entry());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_v2_symbolized() {
        let entry: LogEntry = serde_json::from_str(V2_SYMBOLIZED_LOG_ENTRY).unwrap();
        assert_eq!(entry, symbolized_v2_entry());
    }
}
