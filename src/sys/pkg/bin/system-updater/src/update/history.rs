// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::update::BuildInfo,
    anyhow::Error,
    fidl_fuchsia_paver::{BootManagerProxy, DataSinkProxy},
    fidl_fuchsia_update_installer_ext::State,
    fuchsia_url::pkg_url::PkgUrl,
    futures::prelude::*,
    serde::{Deserialize, Serialize},
    std::{collections::VecDeque, time::SystemTime},
};

mod version;
pub use version::Version;

const UPDATE_HISTORY_PATH: &str = "/data/update_history.json";
const MAX_UPDATE_ATTEMPTS: usize = 5;

/// Wrapper for the versioned UpdateAttempt JSON.
#[derive(Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum UpdateHistoryJson {
    #[serde(rename = "1")]
    Version1(VecDeque<UpdateAttempt>),
}

/// Persistent metadata for recent update attempt.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
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

impl UpdateAttempt {
    fn attempt_id(&self) -> &str {
        &self.attempt_id
    }
    fn update_url(&self) -> &PkgUrl {
        &self.update_url
    }
    fn options(&self) -> &UpdateOptions {
        &self.options
    }
    fn state(&self) -> &State {
        &self.state
    }
}

impl From<&UpdateAttempt> for fidl_fuchsia_update_installer::UpdateResult {
    fn from(attempt: &UpdateAttempt) -> Self {
        Self {
            attempt_id: Some(attempt.attempt_id().to_string()),
            url: Some(fidl_fuchsia_pkg::PackageUrl { url: attempt.update_url().to_string() }),
            options: Some(attempt.options().into()),
            state: Some(attempt.state().clone().into()),
        }
    }
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
    pub fn source_version(&self) -> &Version {
        &self.source_version
    }

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

// TODO(fxb/55401): replace this struct with the one in fidl-fuchsia-update-installer-ext.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct UpdateOptions;

// TODO(fxb/55401): move this to fidl-fuchsia-update-installer-ext.
impl From<&UpdateOptions> for fidl_fuchsia_update_installer::Options {
    fn from(_options: &UpdateOptions) -> Self {
        Self {
            initiator: None,
            allow_attach_to_existing_attempt: None,
            should_write_recovery: None,
        }
    }
}

#[derive(Debug, Default, PartialEq)]
pub struct UpdateHistory {
    update_attempts: VecDeque<UpdateAttempt>,
}

impl UpdateHistory {
    /// The number of sequential attempts to update from source_version to target_version, or 0 if
    /// the most recent update attempt does not match the given source_version/target_version.
    pub fn attempts_for(&self, source_version: &Version, target_version: &Version) -> u32 {
        self.update_attempts
            .iter()
            .take_while(|attempt| {
                &attempt.source_version == source_version
                    && &attempt.target_version == target_version
            })
            .count() as u32
    }

    /// Returns the target version of the most recent successful update attempt, aka, the version
    /// that likely corresponds with this running system.
    fn latest_successful_target_version(&self) -> Option<&Version> {
        self.update_attempts
            .iter()
            .find(|attempt| attempt.state.is_success())
            .map(|attempt| &attempt.target_version)
    }

    /// Start a new update attempt, returns an PendingAttempt with initial information, caller
    /// should fill in additional information and pass it back to `record_update_attempt()`.
    #[must_use]
    pub fn start_update_attempt<'a>(
        &self,
        options: UpdateOptions,
        update_url: PkgUrl,
        start_time: SystemTime,
        data_sink: &'a DataSinkProxy,
        boot_manager: &'a BootManagerProxy,
        build_info: &'a impl BuildInfo,
        pkgfs_system: &'a Option<pkgfs::system::Client>,
    ) -> impl Future<Output = PendingAttempt> + 'a {
        let latest_target_version = self.latest_successful_target_version().cloned();

        async move {
            // Generate a random UUID like 6fa28c38-d149-4c8b-a1fc-2cdbc714aad2.
            let attempt_id = uuid::Uuid::new_v4().to_string();
            let source_version = Version::current(
                latest_target_version.as_ref(),
                data_sink,
                boot_manager,
                build_info,
                pkgfs_system,
            )
            .await;
            PendingAttempt { attempt_id, source_version, options, update_url, start_time }
        }
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
    pub fn save(&mut self) -> impl Future<Output = ()> {
        let writer = |bytes| async move {
            let _ = io_util::file::write_in_namespace(UPDATE_HISTORY_PATH, &bytes).await;
        };
        self.save_to(writer)
    }

    fn save_to<W, F>(&self, writer: W) -> impl Future<Output = ()>
    where
        W: FnOnce(Vec<u8>) -> F,
        F: Future<Output = ()>,
    {
        let history = UpdateHistoryJson::Version1(self.update_attempts.clone());
        let bytes = serde_json::to_vec(&history);

        async move {
            match bytes {
                Ok(bytes) => writer(bytes).await,
                Err(_) => {}
            }
        }
    }

    pub fn last_update_attempt(&self) -> Option<&UpdateAttempt> {
        self.update_attempts.front()
    }

    pub fn update_attempt(&self, attempt_id: String) -> Option<&UpdateAttempt> {
        self.update_attempts.iter().find(|attempt| attempt.attempt_id == attempt_id)
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
        super::{version::mock_pkgfs_system, *},
        crate::update::environment::NamespaceBuildInfo,
        anyhow::anyhow,
        fidl_fuchsia_update_installer_ext::{Progress, UpdateInfo},
        mock_paver::MockPaverServiceBuilder,
        pretty_assertions::assert_eq,
        serde_json::json,
        std::{sync::Arc, time::Duration},
    };

    fn make_reboot_state() -> State {
        let info = UpdateInfo::builder().download_size(42).build();
        let progress = Progress::done(&info);

        State::Reboot { info, progress }
    }

    fn make_reader(res: &str) -> impl Future<Output = Result<Vec<u8>, Error>> {
        future::ready(Ok(res.as_bytes().to_owned()))
    }

    async fn record_fake_update_attempt(history: &mut UpdateHistory, i: usize) {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();
        let boot_manager = paver.spawn_boot_manager_service();
        let (pkgfs_system, _pkgfs_dir) = mock_pkgfs_system("").await;
        let update_attempt = history
            .start_update_attempt(
                UpdateOptions,
                "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                SystemTime::UNIX_EPOCH + Duration::from_nanos(i as u64),
                &data_sink,
                &boot_manager,
                &NamespaceBuildInfo,
                &Some(pkgfs_system),
            )
            .await;
        history.record_update_attempt(
            update_attempt.finish(Version::for_hash("new".to_owned()), State::FailPrepare),
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
        let completed_target_version = Version {
            update_hash: "completed target version".to_string(),
            vbmeta_hash: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
                .to_string(),
            zbi_hash: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
                .to_string(),
            ..Version::default()
        };
        let history = UpdateHistory {
            update_attempts: VecDeque::from(vec![
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old".to_owned()),
                    target_version: Version::for_hash("failed target version".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: State::FailPrepare,
                },
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old".to_owned()),
                    target_version: completed_target_version.clone(),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: make_reboot_state(),
                },
            ]),
        };
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();
        let boot_manager = paver.spawn_boot_manager_service();
        let (pkgfs_system, _pkgfs_dir) = mock_pkgfs_system("").await;
        assert_eq!(
            history
                .start_update_attempt(
                    UpdateOptions,
                    "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    &data_sink,
                    &boot_manager,
                    &NamespaceBuildInfo,
                    &Some(pkgfs_system),
                )
                .await
                .source_version,
            completed_target_version
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn record_update_attempt_and_then_save() {
        let (send, recv) = futures::channel::oneshot::channel();

        let mut history = UpdateHistory::default();
        record_fake_update_attempt(&mut history, 42).await;
        let update_attempt = UpdateAttempt {
            attempt_id: history.update_attempts[0].attempt_id.clone(),
            source_version: Version {
                vbmeta_hash: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
                    .to_string(),
                zbi_hash: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
                    .to_string(),
                ..Version::default()
            },
            target_version: Version::for_hash("new".to_owned()),
            options: UpdateOptions,
            update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
            start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
            state: State::FailPrepare,
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
                    "source": {
                        "update_hash": "",
                        "system_image_hash": "",
                        "vbmeta_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                        "zbi_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                        "build_version": ""
                    },
                    "target": {
                        "update_hash": "new",
                        "system_image_hash": "",
                        "vbmeta_hash": "",
                        "zbi_hash": "",
                        "build_version": ""
                    },
                    "options": null,
                    "url": "fuchsia-pkg://fuchsia.com/update",
                    "start": 42,
                    "state": {
                        "id": "fail_prepare",
                    }
                }]
            }),
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn record_update_attempt_only_keep_max_size() {
        let mut history = UpdateHistory::default();
        for i in 0..10 {
            record_fake_update_attempt(&mut history, i).await;
        }

        assert_eq!(
            history.update_attempts,
            (10 - MAX_UPDATE_ATTEMPTS..10)
                .rev()
                .map(|i| UpdateAttempt {
                    attempt_id: history.update_attempts[9 - i].attempt_id.clone(),
                    source_version: Version {
                        vbmeta_hash:
                            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
                                .to_string(),
                        zbi_hash:
                            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
                                .to_string(),
                        ..Version::default()
                    },
                    target_version: Version::for_hash("new".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(i as u64),
                    state: State::FailPrepare,
                })
                .collect::<VecDeque<_>>()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn empty_history_0_attempts() {
        let history = UpdateHistory::default();
        assert_eq!(
            history.attempts_for(&Version::for_hash("source"), &Version::for_hash("target")),
            0
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn single_attempt_history_has_1_attempt_to_new_version() {
        let mut history = UpdateHistory::default();
        record_fake_update_attempt(&mut history, 42).await;

        assert_eq!(
            history.attempts_for(
                &Version::for_hash_and_empty_paver_hashes(""),
                &Version::for_hash("new")
            ),
            1
        );
        assert_eq!(
            history.attempts_for(
                &Version::for_hash_and_empty_paver_hashes(""),
                &Version::for_hash("newer")
            ),
            0
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn matching_version_attempts_history_max_attempts() {
        let mut history = UpdateHistory::default();
        for i in 0..MAX_UPDATE_ATTEMPTS {
            record_fake_update_attempt(&mut history, i).await;
        }

        assert_eq!(
            history.attempts_for(
                &Version::for_hash_and_empty_paver_hashes(""),
                &Version::for_hash("new")
            ),
            MAX_UPDATE_ATTEMPTS as u32
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn attempts_for_only_matches_most_recent_attempts() {
        let history = UpdateHistory {
            update_attempts: VecDeque::from(vec![
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old"),
                    target_version: Version::for_hash("new"),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: make_reboot_state(),
                },
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old2"),
                    target_version: Version::for_hash("new2"),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: make_reboot_state(),
                },
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old"),
                    target_version: Version::for_hash("new"),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: make_reboot_state(),
                },
            ]),
        };

        assert_eq!(history.attempts_for(&Version::for_hash("old"), &Version::for_hash("new")), 1);
        assert_eq!(history.attempts_for(&Version::for_hash("old2"), &Version::for_hash("new2")), 0);
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
                    state: make_reboot_state(),
                },
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old2".to_owned()),
                    target_version: Version::for_hash("new".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: make_reboot_state(),
                },
            ]),
        };

        assert_eq!(history.attempts_for(&Version::for_hash("old"), &Version::for_hash("new")), 1);
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
                    state: make_reboot_state(),
                },
                UpdateAttempt {
                    attempt_id: "id".to_owned(),
                    source_version: Version::for_hash("old".to_owned()),
                    target_version: Version::for_hash("new2".to_owned()),
                    options: UpdateOptions,
                    update_url: "fuchsia-pkg://fuchsia.com/update".parse().unwrap(),
                    start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
                    state: make_reboot_state(),
                },
            ]),
        };

        assert_eq!(history.attempts_for(&Version::for_hash("old"), &Version::for_hash("new")), 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn last_update_attempt() {
        let first_attempt = UpdateAttempt {
            attempt_id: "0".to_owned(),
            source_version: Version::for_hash("old".to_owned()),
            target_version: Version::for_hash("new".to_owned()),
            options: UpdateOptions,
            update_url: "fuchsia-pkg://fuchsia.com/first-attempt".parse().unwrap(),
            start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
            state: make_reboot_state(),
        };
        let second_attempt = UpdateAttempt {
            attempt_id: "1".to_owned(),
            source_version: Version::for_hash("old".to_owned()),
            target_version: Version::for_hash("new2".to_owned()),
            options: UpdateOptions,
            update_url: "fuchsia-pkg://fuchsia.com/second-attempt".parse().unwrap(),
            start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
            state: make_reboot_state(),
        };
        let mut history = UpdateHistory::default();

        history.record_update_attempt(first_attempt);
        history.record_update_attempt(second_attempt.clone());

        assert_eq!(history.last_update_attempt(), Some(&second_attempt));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn update_attempt() {
        let attempt_foo = UpdateAttempt {
            attempt_id: "foo".to_owned(),
            source_version: Version::for_hash("old".to_owned()),
            target_version: Version::for_hash("new".to_owned()),
            options: UpdateOptions,
            update_url: "fuchsia-pkg://fuchsia.com/first-attempt".parse().unwrap(),
            start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
            state: make_reboot_state(),
        };
        let attempt_bar = UpdateAttempt {
            attempt_id: "bar".to_owned(),
            source_version: Version::for_hash("old".to_owned()),
            target_version: Version::for_hash("new2".to_owned()),
            options: UpdateOptions,
            update_url: "fuchsia-pkg://fuchsia.com/second-attempt".parse().unwrap(),
            start_time: SystemTime::UNIX_EPOCH + Duration::from_nanos(42),
            state: make_reboot_state(),
        };
        let mut history = UpdateHistory::default();

        history.record_update_attempt(attempt_foo.clone());
        history.record_update_attempt(attempt_bar);

        assert_eq!(history.update_attempt("foo".to_owned()), Some(&attempt_foo));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn last_update_attempt_none() {
        let history = UpdateHistory::default();

        assert_eq!(history.last_update_attempt(), None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn update_attempt_none() {
        let history = UpdateHistory::default();

        assert_eq!(history.update_attempt("foo".to_owned()), None);
    }
}
