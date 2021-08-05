// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::environment::Environment,
    crate::file_backed_config::FileBacked as Config,
    crate::runtime::populate_runtime_config,
    anyhow::{anyhow, Result},
    async_lock::RwLock,
    serde_json::Value,
    std::sync::Arc,
    std::{
        collections::HashMap,
        path::PathBuf,
        sync::{Mutex, Once},
        time::{Duration, Instant},
    },
};

// Timeout for the config cache.
pub const CONFIG_CACHE_TIMEOUT: Duration = Duration::from_secs(3);

struct CacheItem {
    created: Instant,
    config: Arc<RwLock<Config>>,
}

type Cache = RwLock<HashMap<Option<String>, CacheItem>>;

static INIT: Once = Once::new();

lazy_static::lazy_static! {
    static ref ENV_FILE: Mutex<Option<PathBuf>> = Mutex::new(None);
    static ref RUNTIME: Mutex<Option<Value>> = Mutex::new(None);
    static ref CACHE: Cache = RwLock::new(HashMap::new());
}

#[cfg(not(test))]
pub fn env_file() -> Option<PathBuf> {
    ENV_FILE.lock().unwrap().as_ref().map(|p| p.to_path_buf())
}

pub fn test_env_file() -> PathBuf {
    use tempfile::NamedTempFile;
    lazy_static::lazy_static! {
        static ref FILE: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
    }
    Environment::init_env_file(&FILE.path().to_path_buf()).expect("initializing env file");
    FILE.path().to_path_buf()
}

#[cfg(test)]
pub fn env_file() -> Option<PathBuf> {
    Some(test_env_file())
}

/// Initialize the configuration. If env is not given, a new test environment is configured.  init()
/// is a static once operation. Only the first call in a process runtime takes effect, so users must
/// call this early with the required values, such as in main() in the ffx binary.
pub fn init(
    runtime: &[String],
    runtime_overrides: Option<String>,
    env: Option<PathBuf>,
) -> Result<()> {
    let mut ret = Ok(());

    // If it's already been initialize, just fail silently. This will allow a setup method to be
    // called by unit tests over and over again without issue.
    INIT.call_once(|| {
        ret = init_impl(runtime, runtime_overrides, env);
    });

    ret
}

fn init_impl(
    runtime: &[String],
    runtime_overrides: Option<String>,
    env: Option<PathBuf>,
) -> Result<()> {
    let env = env.unwrap_or_else(|| test_env_file());

    let mut populated_runtime = Value::Null;
    runtime.iter().chain(&runtime_overrides).try_for_each(|r| {
        if let Some(v) = populate_runtime_config(&Some(r.clone()))? {
            crate::api::value::merge(&mut populated_runtime, &v)
        };
        Result::<()>::Ok(())
    })?;
    match populated_runtime {
        Value::Null => {}
        _ => {
            RUNTIME.lock().unwrap().replace(populated_runtime);
        }
    }

    if !env.is_file() {
        log::debug!("initializing environment {}", env.display());
        Environment::init_env_file(&env)?;
    }
    ENV_FILE.lock().unwrap().replace(env);
    Ok(())
}

fn is_cache_item_expired(item: &CacheItem, now: Instant) -> bool {
    now.checked_duration_since(item.created).map_or(false, |t| t > CONFIG_CACHE_TIMEOUT)
}

async fn read_cache(
    build_dir: &Option<String>,
    now: Instant,
    cache: &Cache,
) -> Option<Arc<RwLock<Config>>> {
    let read_guard = cache.read().await;
    (*read_guard)
        .get(build_dir)
        .filter(|item| !is_cache_item_expired(item, now))
        .map(|item| item.config.clone())
}

pub(crate) async fn load_config(build_dir: &Option<String>) -> Result<Arc<RwLock<Config>>> {
    load_config_with_instant(build_dir, Instant::now(), &CACHE).await
}

async fn load_config_with_instant(
    build_dir: &Option<String>,
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
                    .get(build_dir)
                    .map_or(true, |item| is_cache_item_expired(item, now));
                if write {
                    let runtime = RUNTIME.lock().unwrap().as_ref().map(|v| v.clone());

                    let env = match ENV_FILE.lock().unwrap().as_ref() {
                        Some(path) => match Environment::load(path) {
                            Ok(env) => env,
                            Err(err) => {
                                log::error!(
                                    "failed to load environment, reverting to default: {}",
                                    err
                                );
                                Environment::default()
                            }
                        },
                        None => Environment::default(),
                    };

                    (*write_guard).insert(
                        build_dir.as_ref().cloned(),
                        CacheItem {
                            created: now,
                            config: Arc::new(RwLock::new(Config::new(&env, build_dir, runtime)?)),
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

    async fn load(now: Instant, key: &Option<String>, cache: &Cache) {
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
        key: &Option<String>,
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

    fn setup_build_dirs(tests: usize) -> Vec<Option<String>> {
        let mut build_dirs = Vec::new();
        build_dirs.push(None);
        for x in 0..tests - 1 {
            build_dirs.push(Some(format!("test {}", x)));
        }
        build_dirs
    }

    fn setup(tests: usize) -> (Instant, Vec<Option<String>>, Cache) {
        (Instant::now(), setup_build_dirs(tests), RwLock::new(HashMap::new()))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_one_at_a_time() {
        let tests = 10;
        let (now, build_dirs, cache) = setup(tests);
        for x in 0..tests {
            load_and_test(now, x, x + 1, &build_dirs[x], &cache).await;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_many_at_a_time() {
        let tests = 25;
        let (now, build_dirs, cache) = setup(tests);
        let futures = build_dirs.iter().map(|x| load(now, &x, &cache));
        let result = join_all(futures).await;
        assert_eq!(tests, result.len());
        let read_guard = cache.read().await;
        assert_eq!(tests, (*read_guard).len());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_timeout() {
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

    #[test]
    fn test_expiration_check_does_not_panic() -> Result<()> {
        let tests = 1;
        let (now, build_dirs, _cache) = setup(tests);
        let later = now.checked_add(Duration::from_millis(1)).expect("timeout should not overflow");
        let item = CacheItem {
            created: later,
            config: Arc::new(RwLock::new(Config::new(
                &Environment::default(),
                &build_dirs[0],
                None,
            )?)),
        };
        assert!(!is_cache_item_expired(&item, now));
        Ok(())
    }
}
