// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::environment::Environment,
    crate::runtime::populate_runtime_config,
    crate::storage::{Config, ConfigMap},
    anyhow::{anyhow, Context, Result},
    async_lock::RwLock,
    serde_json::Value,
    std::sync::Arc,
    std::{
        collections::HashMap,
        path::{Path, PathBuf},
        sync::Mutex,
        time::{Duration, Instant},
    },
    tempfile::NamedTempFile,
};

// Timeout for the config cache.
pub const CONFIG_CACHE_TIMEOUT: Duration = Duration::from_secs(3);

struct CacheItem {
    created: Instant,
    config: Arc<RwLock<Config>>,
}

type Cache = RwLock<HashMap<Option<PathBuf>, CacheItem>>;

lazy_static::lazy_static! {
    static ref ENV_FILE: Mutex<Option<PathBuf>> = Mutex::default();
    static ref RUNTIME: Mutex<ConfigMap> = Mutex::default();
    static ref CACHE: Cache = RwLock::default();
}

pub fn env_file() -> Option<PathBuf> {
    ENV_FILE.lock().unwrap().as_ref().map(|p| p.to_path_buf())
}

/// A structure that holds information about the test config environment for the duration
/// of a test. This object must continue to exist for the duration of the test, or the test
/// may fail.
#[must_use = "This object must be held for the duration of a test (ie. `let _env = ffx_config::test_init()`) for it to operate correctly."]
pub struct TestEnv {
    env_file: NamedTempFile,
    _user_file: NamedTempFile,
    _guard: async_lock::MutexGuardArc<()>,
}

impl TestEnv {
    async fn new(_guard: async_lock::MutexGuardArc<()>) -> Result<Self> {
        let env_file = NamedTempFile::new().context("tmp access failed")?;
        let user_file = NamedTempFile::new().context("tmp access failed")?;
        Environment::init_env_file(env_file.path()).await.context("initializing env file")?;

        // Point the user config at a temporary file.
        let mut env = Environment::load(env_file.path()).await.context("opening env file")?;
        env.set_user(Some(user_file.path()));
        env.save().await.context("saving env file")?;

        Ok(TestEnv { env_file, _user_file: user_file, _guard })
    }
}

impl Drop for TestEnv {
    fn drop(&mut self) {
        // after the test, wipe out all the test configuration we set up. Explode if things aren't as we
        // expect them.
        let mut env = ENV_FILE.lock().expect("Poisoned lock");
        let env_prev = env.clone();
        *env = None;
        drop(env);

        let mut runtime = RUNTIME.lock().expect("Poisoned lock");
        let runtime_prev = runtime.clone();
        *runtime = ConfigMap::default();
        drop(runtime);

        // do these checks outside the mutex lock so we can't poison them.
        assert_eq!(
            env_prev.as_deref(),
            Some(self.env_file.path()),
            "env path changed from {expected} to {other:?} during test run somehow.",
            expected = self.env_file.path().display(),
            other = env_prev
        );
        assert_eq!(
            runtime_prev,
            ConfigMap::default(),
            "runtime args changed from an empty map to {other:?} during test run somehow.",
            other = runtime_prev
        );

        // since we're not running in async context during drop, we can't clear the cache unfortunately.
    }
}

/// Initialize the configuration. Only the first call in a process runtime takes effect, so users must
/// call this early with the required values, such as in main() in the ffx binary.
pub async fn init(
    runtime: &[String],
    runtime_overrides: Option<String>,
    env: PathBuf,
) -> Result<()> {
    // explode if we ever try to overwrite the global configuration
    assert!(
        init_impl(runtime, runtime_overrides, env).await?.is_none(),
        "Tried to re-initialize the global configuration outside of a test!"
    );
    Ok(())
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
    init_impl(&[], None, env.env_file.path().to_owned()).await?;
    // force clearing the cache as well
    invalidate().await;

    Ok(env)
}

/// Invalidate the cache. Call this if you do anything that might make a cached config go stale
/// in a critical way, like changing the environment.
pub async fn invalidate() {
    CACHE.write().await.clear();
}

/// Actual implementation of initializing the global environment. Returns the previously set env
/// file, if there is one, or an error if something actionable went wrong.
async fn init_impl(
    runtime: &[String],
    runtime_overrides: Option<String>,
    env: PathBuf,
) -> Result<Option<PathBuf>> {
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
    *RUNTIME.lock().expect("Poisoned lock") = populated_runtime;

    if !env.is_file() {
        tracing::debug!("initializing environment {}", env.display());
        Environment::init_env_file(&env).await?;
    }
    Ok(ENV_FILE.lock().unwrap().replace(env))
}

fn is_cache_item_expired(item: &CacheItem, now: Instant) -> bool {
    now.checked_duration_since(item.created).map_or(false, |t| t > CONFIG_CACHE_TIMEOUT)
}

async fn read_cache(
    build_dir: Option<&Path>,
    now: Instant,
    cache: &Cache,
) -> Option<Arc<RwLock<Config>>> {
    let read_guard = cache.read().await;
    (*read_guard)
        .get(&build_dir.map(Path::to_owned)) // TODO(mgnb): get rid of this allocation when we can
        .filter(|item| !is_cache_item_expired(item, now))
        .map(|item| item.config.clone())
}

pub(crate) async fn load_config(build_dir: Option<&Path>) -> Result<Arc<RwLock<Config>>> {
    load_config_with_instant(build_dir, Instant::now(), &CACHE).await
}

async fn load_config_with_instant(
    build_dir: Option<&Path>,
    now: Instant,
    cache: &Cache,
) -> Result<Arc<RwLock<Config>>> {
    let cache_hit = read_cache(build_dir, now, cache).await;
    match cache_hit {
        Some(h) => Ok(h),
        None => {
            {
                let mut write_guard = cache.write().await;
                // Check again if in the time it took to get the lock this config was written
                let write = (*write_guard)
                    .get(&build_dir.map(Path::to_owned)) // TODO(mgnb): get rid of this allocation when we can
                    .map_or(true, |item| is_cache_item_expired(item, now));
                if write {
                    let runtime = RUNTIME.lock().unwrap().clone();
                    let env_path = env_file().context(
                        "Tried to load from config cache with no environment configured.",
                    )?;

                    let env = match Environment::load(&env_path).await {
                        Ok(env) => env,
                        Err(err) => {
                            tracing::error!(
                                "failed to load environment, reverting to default: {}",
                                err
                            );
                            Environment::new_empty(Some(&env_path))
                        }
                    };

                    (*write_guard).insert(
                        build_dir.map(Path::to_owned), // TODO(mgnb): get rid of this allocation when we can,
                        CacheItem {
                            created: now,
                            config: Arc::new(RwLock::new(Config::from_env(
                                &env, build_dir, runtime,
                            )?)),
                        },
                    );
                }
            }
            read_cache(build_dir, now, cache)
                .await
                .ok_or(anyhow!("reading config value from cache after initialization"))
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use {super::*, futures::future::join_all, std::time::Duration};

    async fn load(now: Instant, key: Option<&Path>, cache: &Cache) {
        let tests = 25;
        let mut futures = Vec::new();
        for _x in 0..tests {
            futures.push(load_config_with_instant(key, now, cache));
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
        key: Option<&Path>,
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
        let _env = test_init().await.unwrap();
        let tests = 10;
        let (now, build_dirs, cache) = setup(tests);
        for x in 0..tests {
            load_and_test(now, x, x + 1, build_dirs[x].as_deref(), &cache).await;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_many_at_a_time() {
        let _env = test_init().await.unwrap();
        let tests = 25;
        let (now, build_dirs, cache) = setup(tests);
        let futures = build_dirs.iter().map(|x| load(now, x.as_deref(), &cache));
        let result = join_all(futures).await;
        assert_eq!(tests, result.len());
        let read_guard = cache.read().await;
        assert_eq!(tests, (*read_guard).len());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_timeout() {
        let _env = test_init().await.unwrap();
        let tests = 1;
        let (now, build_dirs, cache) = setup(tests);
        load_and_test(now, 0, 1, build_dirs[0].as_deref(), &cache).await;
        let timeout = now.checked_add(CONFIG_CACHE_TIMEOUT).expect("timeout should not overflow");
        let after_timeout = timeout
            .checked_add(Duration::from_millis(1))
            .expect("after timeout should not overflow");
        load_and_test(timeout, 1, 1, build_dirs[0].as_deref(), &cache).await;
        load_and_test(after_timeout, 1, 1, build_dirs[0].as_deref(), &cache).await;
    }

    #[test]
    fn test_expiration_check_does_not_panic() -> Result<()> {
        let tests = 1;
        let (now, build_dirs, _cache) = setup(tests);
        let later = now.checked_add(Duration::from_millis(1)).expect("timeout should not overflow");
        let item = CacheItem {
            created: later,
            config: Arc::new(RwLock::new(Config::from_env(
                &Environment::default(),
                build_dirs[0].as_deref(),
                ConfigMap::default(),
            )?)),
        };
        assert!(!is_cache_item_expired(&item, now));
        Ok(())
    }
}
