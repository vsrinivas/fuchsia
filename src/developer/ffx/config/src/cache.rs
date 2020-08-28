// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{self, CONFIG_CACHE_TIMEOUT},
    crate::environment::Environment,
    crate::file_backed_config::FileBacked as Config,
    crate::runtime::populate_runtime_config,
    anyhow::{anyhow, Context, Result},
    async_std::sync::{Arc, RwLock},
    serde_json::Value,
    std::{
        collections::HashMap,
        fs::{create_dir_all, File},
        io::Write,
        path::PathBuf,
        sync::{
            atomic::{AtomicBool, Ordering},
            Mutex,
        },
        time::Instant,
    },
};

#[cfg(test)]
use tempfile::NamedTempFile;

struct CacheItem {
    created: Instant,
    config: Arc<RwLock<Config>>,
}

type Cache = RwLock<HashMap<Option<String>, CacheItem>>;

lazy_static::lazy_static! {
    static ref INIT: AtomicBool = AtomicBool::new(false);
    static ref ENV_FILE: Mutex<Option<String>> = Mutex::new(None);
    static ref RUNTIME: Mutex<Option<Value>> = Mutex::new(None);
    static ref CACHE: Cache = RwLock::new(HashMap::new());
}

pub fn get_config_base_path() -> PathBuf {
    let mut path = ffx_core::get_base_path();
    path.push("config");
    create_dir_all(&path).expect("unable to create ffx config directory");
    path
}

#[cfg(not(test))]
pub fn env_file() -> Option<String> {
    ENV_FILE.lock().unwrap().as_ref().map(|v| format!("{}", v))
}

#[cfg(test)]
pub fn env_file() -> Option<String> {
    lazy_static::lazy_static! {
        static ref FILE: NamedTempFile = NamedTempFile::new().expect("tmp access failed");
    }
    init_env_file(&FILE.path().to_path_buf()).expect("initializing env file");
    FILE.path().to_str().map(|s| s.to_string())
}

pub fn init_config(runtime: &Option<String>, env_override: &Option<String>) -> Result<()> {
    // If it's already been initialize, just fail silently. This will allow a setup method to be
    // called by unit tests over and over again without issue.
    if INIT.compare_and_swap(false, true, Ordering::Release) {
        Ok(())
    } else {
        init_config_impl(runtime, env_override)
    }
}

fn init_config_impl(runtime: &Option<String>, env_override: &Option<String>) -> Result<()> {
    let populated_runtime = populate_runtime_config(runtime)?;
    let _ = populated_runtime.and_then(|v| RUNTIME.lock().unwrap().replace(v));

    let env_path = if let Some(f) = env_override {
        PathBuf::from(f)
    } else {
        let mut path = get_config_base_path();
        path.push(constants::ENV_FILE);
        path
    };

    if !env_path.is_file() {
        log::debug!("initializing environment {}", env_path.display());
        init_env_file(&env_path)?;
    }
    env_path.to_str().map(String::from).context("getting environment file").and_then(|e| {
        let _ = ENV_FILE.lock().unwrap().replace(e);
        Ok(())
    })
}

fn init_env_file(path: &PathBuf) -> Result<()> {
    let mut f = File::create(path)?;
    f.write_all(b"{}")?;
    f.sync_all()?;
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
                    (*write_guard).insert(
                        build_dir.as_ref().cloned(),
                        CacheItem {
                            created: now,
                            config: Arc::new(RwLock::new(Config::new(
                                &Environment::try_load(ENV_FILE.lock().unwrap().as_ref()),
                                build_dir,
                                runtime,
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
                &Environment::try_load(None),
                &build_dirs[0],
                None,
            )?)),
        };
        assert!(!is_cache_item_expired(&item, now));
        Ok(())
    }
}
