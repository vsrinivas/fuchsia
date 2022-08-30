// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::runtime::populate_runtime_config;
use crate::storage::{Config, ConfigMap};
use crate::{
    environment::{Environment, EnvironmentContext},
    BuildOverride,
};
use anyhow::{anyhow, bail, Context, Result};
use async_lock::{RwLock, RwLockUpgradableReadGuard};
use serde_json::Value;
use std::{
    collections::HashMap,
    path::{Path, PathBuf},
    sync::Arc,
    sync::Mutex,
    time::{Duration, Instant},
};
use tempfile::{NamedTempFile, TempDir};

// Timeout for the config cache.
pub const CONFIG_CACHE_TIMEOUT: Duration = Duration::from_secs(3);

struct CacheItem {
    created: Instant,
    config: Arc<RwLock<Config>>,
}

type Cache = RwLock<HashMap<Option<PathBuf>, CacheItem>>;

lazy_static::lazy_static! {
    static ref ENV: Mutex<Option<EnvironmentContext>> = Mutex::default();
    static ref CACHE: Cache = RwLock::default();
}

pub fn global_env_context() -> Option<EnvironmentContext> {
    ENV.lock().unwrap().clone()
}

pub async fn global_env() -> Result<Environment> {
    let context =
        global_env_context().context("Tried to load global environment before configuration")?;

    match context.load().await {
        Err(err) => {
            tracing::error!("failed to load environment, reverting to default: {}", err);
            Environment::new_empty(context).await
        }
        Ok(ctx) => Ok(ctx),
    }
}

/// A structure that holds information about the test config environment for the duration
/// of a test. This object must continue to exist for the duration of the test, or the test
/// may fail.
#[must_use = "This object must be held for the duration of a test (ie. `let _env = ffx_config::test_init()`) for it to operate correctly."]
pub struct TestEnv {
    pub env_file: NamedTempFile,
    pub context: EnvironmentContext,
    pub isolate_root: TempDir,
    pub user_file: NamedTempFile,
    _guard: async_lock::MutexGuardArc<()>,
}

impl TestEnv {
    async fn new(_guard: async_lock::MutexGuardArc<()>) -> Result<Self> {
        let env_file = NamedTempFile::new().context("tmp access failed")?;
        let user_file = NamedTempFile::new().context("tmp access failed")?;
        let isolate_root = tempfile::tempdir()?;

        Environment::init_env_file(env_file.path()).await.context("initializing env file")?;

        // Point the user config at a temporary file.
        let user_file_path = user_file.path().to_owned();
        let context = EnvironmentContext::isolated(
            isolate_root.path().to_owned(),
            HashMap::from_iter(std::env::vars()),
            ConfigMap::default(),
            Some(env_file.path().to_owned()),
        );
        let test_env = TestEnv { env_file, context, user_file, isolate_root, _guard };

        let mut env = test_env.load().await;
        env.set_user(Some(&user_file_path));
        env.save().await.context("saving env file")?;

        Ok(test_env)
    }

    pub async fn load(&self) -> Environment {
        self.context.load().await.expect("opening test env file")
    }
}

impl Drop for TestEnv {
    fn drop(&mut self) {
        // after the test, wipe out all the test configuration we set up. Explode if things aren't as we
        // expect them.
        let mut env = ENV.lock().expect("Poisoned lock");
        let env_prev = env.clone();
        *env = None;
        drop(env);

        if let Some(env_prev) = env_prev {
            assert_eq!(
                env_prev,
                self.context,
                "environment context changed from isolated environment to {other:?} during test run somehow.",
                other = env_prev
            );
        }

        // since we're not running in async context during drop, we can't clear the cache unfortunately.
    }
}

/// Initialize the configuration. Only the first call in a process runtime takes effect, so users must
/// call this early with the required values, such as in main() in the ffx binary.
pub async fn init(
    runtime: &[String],
    runtime_overrides: Option<String>,
    env: Option<PathBuf>,
) -> Result<EnvironmentContext> {
    let mut populated_runtime = Value::Null;
    runtime.iter().chain(&runtime_overrides).try_for_each(|r| {
        if let Some(v) = populate_runtime_config(&Some(r.clone()))? {
            crate::api::value::merge(&mut populated_runtime, &v)
        };
        Result::<()>::Ok(())
    })?;
    let populated_runtime = match populated_runtime {
        Value::Null => ConfigMap::default(),
        Value::Object(runtime) => runtime,
        _ => return Err(anyhow!("Invalid runtime configuration: must be an object")),
    };
    let context = EnvironmentContext::detect(populated_runtime, std::env::current_dir()?, env)?;

    init_impl(&context).await?;

    Ok(context)
}

/// When running tests we usually just want to initialize a blank slate configuration, so
/// use this for tests. You must hold the returned object object for the duration of the test, not doing so
/// will result in strange behaviour.
pub async fn test_init() -> Result<TestEnv> {
    lazy_static::lazy_static! {
        static ref TEST_LOCK: Arc<async_lock::Mutex<()>> = Arc::default();
    }
    let env = TestEnv::new(TEST_LOCK.lock_arc().await).await?;

    // force an overwrite of the configuration setup
    init_impl(&env.context).await?;
    // force clearing the cache as well
    invalidate().await;

    Ok(env)
}

/// Invalidate the cache. Call this if you do anything that might make a cached config go stale
/// in a critical way, like changing the environment.
pub async fn invalidate() {
    CACHE.write().await.clear();
}

async fn init_impl(context: &EnvironmentContext) -> Result<()> {
    let env = context.env_file_path()?;
    if !env.is_file() {
        tracing::debug!("initializing environment {}", env.display());
        Environment::init_env_file(&env).await?;
    }
    let mut env_lock = ENV.lock().unwrap();
    if env_lock.is_some() {
        bail!("Attempted to set the global environment more than once in a process invocation, outside of a test");
    }
    env_lock.replace(context.clone());
    Ok(())
}

fn is_cache_item_expired(item: &CacheItem, now: Instant) -> bool {
    now.checked_duration_since(item.created).map_or(false, |t| t > CONFIG_CACHE_TIMEOUT)
}

async fn read_cache(
    guard: &impl std::ops::Deref<Target = HashMap<Option<PathBuf>, CacheItem>>,
    build_dir: Option<&Path>,
    now: Instant,
) -> Option<Arc<RwLock<Config>>> {
    guard
        .get(&build_dir.map(Path::to_owned)) // TODO(mgnb): get rid of this allocation when we can
        .filter(|item| !is_cache_item_expired(item, now))
        .map(|item| item.config.clone())
}

pub(crate) async fn load_config(
    environment: Environment,
    build_override: Option<BuildOverride<'_>>,
) -> Result<Arc<RwLock<Config>>> {
    load_config_with_instant(&environment, build_override, Instant::now(), &CACHE).await
}

async fn load_config_with_instant(
    env: &Environment,
    build_override: Option<BuildOverride<'_>>,
    now: Instant,
    cache: &Cache,
) -> Result<Arc<RwLock<Config>>> {
    let guard = cache.upgradable_read().await;
    let build_dir = env.override_build_dir(build_override);
    let cache_hit = read_cache(&guard, build_dir, now).await;
    match cache_hit {
        Some(h) => Ok(h),
        None => {
            let mut guard = RwLockUpgradableReadGuard::upgrade(guard).await;
            let config = Arc::new(RwLock::new(Config::from_env(&env)?));

            guard.insert(
                build_dir.map(Path::to_owned), // TODO(mgnb): get rid of this allocation when we can,
                CacheItem { created: now, config: config.clone() },
            );
            Ok(config)
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use {super::*, futures::future::join_all, std::time::Duration};

    async fn load(now: Instant, key: &Option<PathBuf>, cache: &Cache) {
        let env = Environment::new_empty(EnvironmentContext::default())
            .await
            .expect("Empty and anonymous environment shouldn't fail to load");
        let tests = 25;
        let mut futures = Vec::new();
        for _x in 0..tests {
            futures.push(load_config_with_instant(
                &env,
                key.as_deref().map(BuildOverride::Path),
                now,
                cache,
            ));
        }
        let result = join_all(futures).await;
        assert_eq!(tests, result.len());
        result.iter().for_each(|x| {
            assert!(x.is_ok());
        });
    }

    async fn load_and_test(
        now: Instant,
        expected_len_before: usize,
        expected_len_after: usize,
        key: &Option<PathBuf>,
        cache: &Cache,
    ) {
        {
            let read_guard = cache.read().await;
            assert_eq!(expected_len_before, (*read_guard).len());
        }
        load(now, key, cache).await;
        {
            let read_guard = cache.read().await;
            assert_eq!(expected_len_after, (*read_guard).len());
        }
    }

    fn setup_build_dirs(tests: usize) -> Vec<Option<PathBuf>> {
        let mut build_dirs = Vec::new();
        build_dirs.push(None);
        for x in 0..tests - 1 {
            build_dirs.push(Some(PathBuf::from(format!("test {}", x))));
        }
        build_dirs
    }

    fn setup(tests: usize) -> (Instant, Vec<Option<PathBuf>>, Cache) {
        (Instant::now(), setup_build_dirs(tests), RwLock::new(HashMap::new()))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_one_at_a_time() {
        let _test_env = test_init().await.unwrap();
        let tests = 10;
        let (now, build_dirs, cache) = setup(tests);
        for x in 0..tests {
            load_and_test(now, x, x + 1, &build_dirs[x], &cache).await;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_many_at_a_time() {
        let _test_env = test_init().await.unwrap();
        let tests = 25;
        let (now, build_dirs, cache) = setup(tests);
        let futures = build_dirs.iter().map(|x| load(now, x, &cache));
        let result = join_all(futures).await;
        assert_eq!(tests, result.len());
        let read_guard = cache.read().await;
        assert_eq!(tests, (*read_guard).len());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_timeout() {
        let _test_env = test_init().await.unwrap();
        let tests = 1;
        let (now, build_dirs, cache) = setup(tests);
        load_and_test(now, 0, 1, &build_dirs[0], &cache).await;
        let timeout = now.checked_add(CONFIG_CACHE_TIMEOUT).expect("timeout should not overflow");
        let after_timeout = timeout
            .checked_add(Duration::from_millis(1))
            .expect("after timeout should not overflow");
        load_and_test(timeout, 1, 1, &build_dirs[0], &cache).await;
        load_and_test(after_timeout, 1, 1, &build_dirs[0], &cache).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_expiration_check_does_not_panic() -> Result<()> {
        let now = Instant::now();
        let later = now.checked_add(Duration::from_millis(1)).expect("timeout should not overflow");
        let cfg = Config::from_env(&Environment::new_empty(EnvironmentContext::default()).await?)?;
        let item = CacheItem { created: later, config: Arc::new(RwLock::new(cfg)) };
        assert!(!is_cache_item_expired(&item, now));
        Ok(())
    }
}
