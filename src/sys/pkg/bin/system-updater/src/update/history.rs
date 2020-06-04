// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    futures::prelude::*,
    serde::{Deserialize, Serialize},
    std::time::SystemTime,
};

const UPDATE_HISTORY_PATH: &str = "/data/update_history.json";

/// Persistent metadata for recent update attempts.
#[derive(Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct UpdateHistory {
    #[serde(rename = "source")]
    source_version: String,

    #[serde(rename = "target")]
    target_version: String,

    #[serde(rename = "start", with = "serde_system_time")]
    start_time: SystemTime,

    attempts: u32,
}

impl UpdateHistory {
    /// The number of sequential attempts to update from source_version to target_version,
    /// including this one.
    pub fn attempts(&self) -> u32 {
        self.attempts
    }

    /// Load the last update history struct from disk, incrementing its attempt counter, or start
    /// over with a fresh history struct if the load fails or this update attempt's source_version
    /// or target_version differ.
    pub async fn increment_or_create(
        source_version: &str,
        target_version: &str,
        start_time: SystemTime,
    ) -> Self {
        let reader = async {
            let file = io_util::file::open_in_namespace(
                UPDATE_HISTORY_PATH,
                io_util::OPEN_RIGHT_READABLE,
            )?;

            let contents = io_util::file::read(&file).await?;

            Ok(contents)
        };

        Self::increment_or_create_from(source_version, target_version, start_time, reader).await
    }

    async fn increment_or_create_from<R>(
        source_version: &str,
        target_version: &str,
        start_time: SystemTime,
        reader: R,
    ) -> Self
    where
        R: Future<Output = Result<Vec<u8>, Error>>,
    {
        let first_attempt = || Self {
            source_version: source_version.to_owned(),
            target_version: target_version.to_owned(),
            start_time,
            attempts: 1,
        };

        let mut history = match Self::load(reader).await {
            Ok(history) => history,
            Err(_) => return first_attempt(),
        };

        if history.source_version != source_version || history.target_version != target_version {
            return first_attempt();
        }

        history.start_time = start_time;
        history.attempts += 1;
        history
    }

    async fn load<R>(reader: R) -> Result<Self, Error>
    where
        R: Future<Output = Result<Vec<u8>, Error>>,
    {
        let contents = reader.await?;
        let history = serde_json::from_slice(&contents)?;
        Ok(history)
    }

    /// Write the update history struct to disk, silently ignoring errors.
    pub async fn save(&self) {
        let writer = |bytes| async move {
            let _ = io_util::file::write_in_namespace(UPDATE_HISTORY_PATH, &bytes).await;
        };

        self.save_to(writer).await;
    }

    async fn save_to<W, F>(&self, writer: W)
    where
        W: FnOnce(Vec<u8>) -> F,
        F: Future<Output = ()>,
    {
        let bytes = match serde_json::to_vec(self) {
            Ok(bytes) => bytes,
            Err(_) => return,
        };

        writer(bytes).await
    }
}

mod serde_system_time {
    use {
        anyhow::{anyhow, Error},
        serde::Deserialize,
        std::{
            convert::TryInto,
            time::{Duration, SystemTime},
        },
    };

    fn system_time_to_nanos(time: SystemTime) -> Result<u64, Error> {
        let nanos = time.duration_since(SystemTime::UNIX_EPOCH)?.as_nanos().try_into()?;
        Ok(nanos)
    }

    fn nanos_to_system_time(nanos: u64) -> SystemTime {
        SystemTime::UNIX_EPOCH + Duration::from_nanos(nanos.into())
    }

    pub fn serialize<S>(time: &SystemTime, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let nanos = system_time_to_nanos(*time).map_err(|e| {
            serde::ser::Error::custom(format!(
                "unable to serialize SystemTime to nanos: {:#}",
                anyhow!(e)
            ))
        })?;

        serializer.serialize_u64(nanos)
    }

    pub fn deserialize<'de, D>(deserializer: D) -> Result<SystemTime, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let nanos = u64::deserialize(deserializer)?;
        let time = nanos_to_system_time(nanos);
        Ok(time)
    }

    #[cfg(test)]
    mod tests {
        use {super::*, proptest::prelude::*};

        proptest! {
            #[test]
            fn roundtrips(nanos: u64) {
                prop_assert_eq!(
                    nanos,
                    system_time_to_nanos(nanos_to_system_time(nanos)).unwrap()
                );
            }

            #[test]
            fn system_time_to_nanos_does_not_panic(time: SystemTime) {
                let _ = system_time_to_nanos(time);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::anyhow, serde_json::json, std::time::Duration};

    fn make_reader(res: &str) -> impl Future<Output = Result<Vec<u8>, Error>> {
        future::ready(Ok(res.as_bytes().to_owned()))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn increments_attempt_if_versions_match() {
        let reader = make_reader(
            &json!({
                "source": "old",
                "target": "new",
                "start": 42,
                "attempts": 1,
            })
            .to_string(),
        );

        let now = SystemTime::now();
        let history = UpdateHistory::increment_or_create_from("old", "new", now, reader).await;

        assert_eq!(
            history,
            UpdateHistory {
                source_version: "old".to_owned(),
                target_version: "new".to_owned(),
                start_time: now,
                attempts: 2,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn uses_default_on_read_error() {
        let now = SystemTime::now();

        let history = UpdateHistory::increment_or_create_from(
            "source",
            "target",
            now,
            future::ready(Err(anyhow!("oops"))),
        )
        .await;
        assert_eq!(
            history,
            UpdateHistory {
                source_version: "source".to_owned(),
                target_version: "target".to_owned(),
                start_time: now,
                attempts: 1,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn uses_default_on_parse_error() {
        let now = SystemTime::now();

        let history = UpdateHistory::increment_or_create_from(
            "source",
            "target",
            now,
            make_reader("not json"),
        )
        .await;
        assert_eq!(
            history,
            UpdateHistory {
                source_version: "source".to_owned(),
                target_version: "target".to_owned(),
                start_time: now,
                attempts: 1,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn uses_default_if_versions_mismatch() {
        let now = SystemTime::now();

        // target version differs
        let reader = make_reader(
            &json!({
                "source": "v1",
                "target": "v2",
                "start": 42,
                "attempts": 1,
            })
            .to_string(),
        );
        assert_eq!(
            UpdateHistory::increment_or_create_from("v1", "v3", now, reader).await,
            UpdateHistory {
                source_version: "v1".to_owned(),
                target_version: "v3".to_owned(),
                start_time: now,
                attempts: 1,
            }
        );

        // source version differs
        let reader = make_reader(
            &json!({
                "source": "v1",
                "target": "v2",
                "start": 42,
                "attempts": 1,
            })
            .to_string(),
        );
        assert_eq!(
            UpdateHistory::increment_or_create_from("v0?", "v2", now, reader).await,
            UpdateHistory {
                source_version: "v0?".to_owned(),
                target_version: "v2".to_owned(),
                start_time: now,
                attempts: 1,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn save_saves() {
        let (send, recv) = futures::channel::oneshot::channel();

        UpdateHistory {
            source_version: "old".to_owned(),
            target_version: "new".to_owned(),
            start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
            attempts: 1,
        }
        .save_to(|bytes| async move {
            send.send(bytes).unwrap();
        })
        .await;

        assert_eq!(
            serde_json::from_slice::<serde_json::Value>(&recv.await.unwrap()).unwrap(),
            json!({
                "source": "old",
                "target": "new",
                "start": 42,
                "attempts": 1,
            }),
        );
    }
}
