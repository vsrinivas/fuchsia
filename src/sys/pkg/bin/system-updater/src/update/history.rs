// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::update::State,
    anyhow::Error,
    fuchsia_url::pkg_url::PkgUrl,
    futures::prelude::*,
    serde::{Deserialize, Serialize},
    std::{collections::VecDeque, time::SystemTime},
    update_package::UpdatePackage,
};

const UPDATE_HISTORY_PATH: &str = "/data/update_history.json";
const MAX_UPDATE_ATTEMPTS: usize = 5;

/// Wrapper for the versioned UpdateAttempt JSON.
#[derive(Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum UpdateHistoryJson {
    #[serde(rename = "1")]
    Version1(VecDeque<UpdateAttempt>),
}

/// Persistent metadata for recent update attempt.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct UpdateAttempt {
    #[serde(rename = "id")]
    attempt_id: String,

    #[serde(rename = "source")]
    source_version: Version,

    #[serde(rename = "target")]
    target_version: Version,

    options: UpdateOptions,

    #[serde(rename = "url")]
    update_url: PkgUrl,

    #[serde(rename = "start", with = "serde_system_time")]
    start_time: SystemTime,

    state: State,
}

/// The pending update attempt that needs to be finished to get a UpdateAttempt.
#[derive(Clone, Debug)]
pub struct PendingAttempt {
    attempt_id: String,

    source_version: Version,

    options: UpdateOptions,

    update_url: PkgUrl,

    start_time: SystemTime,
}

impl PendingAttempt {
    pub fn finish(self, target_version: Version, state: State) -> UpdateAttempt {
        UpdateAttempt {
            attempt_id: self.attempt_id,
            source_version: self.source_version,
            target_version,
            options: self.options,
            update_url: self.update_url,
            start_time: self.start_time,
            state,
        }
    }
}

/// The version of the OS.
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct Version {
    update_hash: String,
}

impl Version {
    #[cfg(test)]
    pub fn for_hash(update_hash: String) -> Self {
        Self { update_hash }
    }

    pub async fn for_update_package(update_package: &UpdatePackage) -> Self {
        match update_package.hash().await {
            Ok(hash) => Self { update_hash: hash.to_string() },
            Err(_) => Self::default(),
        }
    }

    pub fn current(last_target_version: Option<&Version>) -> Self {
        match last_target_version {
            Some(version) => version.clone(),
            None => Self::default(),
        }
    }
}

// TODO(fxb/55401): replace this struct with the one in fidl-fuchsia-update-installer-ext.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct UpdateOptions;

#[derive(Debug, Default, PartialEq, Eq)]
pub struct UpdateHistory {
    update_attempts: VecDeque<UpdateAttempt>,
}

impl UpdateHistory {
    /// The number of sequential attempts to update from source_version to target_version.
    pub fn attempts(&self) -> u32 {
        match self.update_attempts.front() {
            Some(last_attempt) => self
                .update_attempts
                .iter()
                .take_while(|attempt| {
                    attempt.source_version == last_attempt.source_version
                        && attempt.target_version == last_attempt.target_version
                })
                .count() as u32,
            None => 0,
        }
    }

    /// Start a new update attempt, returns an PendingAttempt with initial information, caller
    /// should fill in additional information and pass it back to `record_update_attempt()`.
    #[must_use]
    pub fn start_update_attempt(
        &self,
        options: UpdateOptions,
        update_url: PkgUrl,
        start_time: SystemTime,
    ) -> PendingAttempt {
        // Generate a random UUID like 6fa28c38-d149-4c8b-a1fc-2cdbc714aad2.
        let attempt_id = uuid::Uuid::new_v4().to_string();
        let source_version = Version::current(
            self.update_attempts
                .iter()
                .find(|attempt| attempt.state == State::COMPLETE)
                .map(|attempt| &attempt.target_version),
        );
        PendingAttempt { attempt_id, source_version, options, update_url, start_time }
    }

    /// Record a new update attempt in update history, will drop old attempts if the total
    /// number of attempts exceeds the limit.
    /// Does not write anything to disk, call `.save()` to do that.
    pub fn record_update_attempt(&mut self, update_attempt: UpdateAttempt) {
        self.update_attempts.push_front(update_attempt);
        if self.update_attempts.len() > MAX_UPDATE_ATTEMPTS {
            self.update_attempts.pop_back();
        }
    }

    /// Read the update history struct from disk.
    pub async fn load() -> Self {
        let reader = io_util::file::read_in_namespace(UPDATE_HISTORY_PATH).map_err(|e| e.into());

        Self::load_from_or_default(reader).await
    }

    async fn load_from_or_default<R>(reader: R) -> Self
    where
        R: Future<Output = Result<Vec<u8>, Error>>,
    {
        Self::load_from(reader).await.unwrap_or_else(|_| Self::default())
    }

    async fn load_from<R>(reader: R) -> Result<Self, Error>
    where
        R: Future<Output = Result<Vec<u8>, Error>>,
    {
        let contents = reader.await?;
        let history: UpdateHistoryJson = serde_json::from_slice(&contents)?;
        match history {
            UpdateHistoryJson::Version1(update_attempts) => Ok(Self { update_attempts }),
        }
    }

    /// Save the update history to disk.
    pub async fn save(&mut self) {
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
        let history = UpdateHistoryJson::Version1(self.update_attempts.clone());
        let bytes = match serde_json::to_vec(&history) {
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
    use {
        super::*, anyhow::anyhow, pretty_assertions::assert_eq, serde_json::json,
        std::time::Duration,
    };

    fn make_reader(res: &str) -> impl Future<Output = Result<Vec<u8>, Error>> {
        future::ready(Ok(res.as_bytes().to_owned()))
    }

    fn record_fake_update_attempt(history: &mut UpdateHistory, i: usize) {
        let update_attempt = history.start_update_attempt(
            UpdateOptions,
            "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
            SystemTime::UNIX_EPOCH + Duration::from_nanos(i as u64),
        );
        history.record_update_attempt(
            update_attempt.finish(Version::for_hash("new".to_owned()), State::FAIL),
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn uses_default_on_read_error() {
        let history =
            UpdateHistory::load_from_or_default(future::ready(Err(anyhow!("oops")))).await;
        assert_eq!(history, UpdateHistory { update_attempts: VecDeque::new() });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn uses_default_on_parse_error() {
        let history = UpdateHistory::load_from_or_default(make_reader("not json")).await;
        assert_eq!(history, UpdateHistory { update_attempts: VecDeque::new() });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn start_update_attempt_uses_last_successful_attempt_as_source_version() {
        let history = UpdateHistory {
            update_attempts: VecDeque::from(vec![
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old".to_owned()),
                    target_version: Version::for_hash("failed target version".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: State::FAIL,
                },
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old".to_owned()),
                    target_version: Version::for_hash("completed target version".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: State::COMPLETE,
                },
            ]),
        };
        assert_eq!(
            history
                .start_update_attempt(
                    UpdateOptions,
                    "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    SystemTime::UNIX_EPOCH + Duration::from_nanos(42)
                )
                .source_version,
            Version::for_hash("completed target version".to_owned())
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn record_update_attempt_and_then_save() {
        let (send, recv) = futures::channel::oneshot::channel();

        let mut history = UpdateHistory::default();
        record_fake_update_attempt(&mut history, 42);
        let update_attempt = UpdateAttempt {
            attempt_id: history.update_attempts[0].attempt_id.clone(),
            source_version: Version::default(),
            target_version: Version::for_hash("new".to_owned()),
            options: UpdateOptions,
            update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
            start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
            state: State::FAIL,
        };
        assert_eq!(history.update_attempts, VecDeque::from(vec![update_attempt]));

        history
            .save_to(|bytes| async move {
                send.send(bytes).unwrap();
            })
            .await;

        assert_eq!(
            serde_json::from_slice::<serde_json::Value>(&recv.await.unwrap()).unwrap(),
            json!({
                "version": "1",
                "content": [{
                    "id": history.update_attempts[0].attempt_id.clone(),
                    "source": {"update_hash": ""},
                    "target": {"update_hash": "new"},
                    "options": null,
                    "url": "fuchsia-pkg://fuchsia.com/update",
                    "start": 42,
                    "state": "FAIL",
                }]
            }),
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn record_update_attempt_only_keep_max_size() {
        let mut history = UpdateHistory::default();
        for i in 0..10 {
            record_fake_update_attempt(&mut history, i);
        }

        assert_eq!(
            history.update_attempts,
            (10 - MAX_UPDATE_ATTEMPTS..10)
                .rev()
                .map(|i| UpdateAttempt {
                    attempt_id: history.update_attempts[9 - i].attempt_id.clone(),
                    source_version: Version::default(),
                    target_version: Version::for_hash("new".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(i as u64),
                    state: State::FAIL,
                })
                .collect::<VecDeque<_>>()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn empty_history_0_attempts() {
        let history = UpdateHistory::default();
        assert_eq!(history.attempts(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn single_attempt_history_1_attempt() {
        let mut history = UpdateHistory::default();
        record_fake_update_attempt(&mut history, 42);
        assert_eq!(history.attempts(), 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn matching_version_attempts_history_max_attempts() {
        let mut history = UpdateHistory::default();
        for i in 0..MAX_UPDATE_ATTEMPTS {
            record_fake_update_attempt(&mut history, i);
        }

        assert_eq!(history.attempts(), MAX_UPDATE_ATTEMPTS as u32);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn mismatching_source_version_attempts_history_1_attempt() {
        let history = UpdateHistory {
            update_attempts: VecDeque::from(vec![
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old".to_owned()),
                    target_version: Version::for_hash("new".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: State::COMPLETE,
                },
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old2".to_owned()),
                    target_version: Version::for_hash("new".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: State::COMPLETE,
                },
            ]),
        };

        assert_eq!(history.attempts(), 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn mismatching_target_version_attempts_history_1_attempt() {
        let history = UpdateHistory {
            update_attempts: VecDeque::from(vec![
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old".to_owned()),
                    target_version: Version::for_hash("new".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: State::COMPLETE,
                },
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old".to_owned()),
                    target_version: Version::for_hash("new2".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: State::COMPLETE,
                },
            ]),
        };

        assert_eq!(history.attempts(), 1);
    }
}
