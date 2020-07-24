// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::Config,
    crate::constants::CONFIG_CACHE_TIMEOUT,
    crate::environment::Environment,
    crate::heuristic_config::HeuristicFn,
    anyhow::{anyhow, Result},
    async_std::sync::{Arc, RwLock},
    ffx_lib_args::Ffx,
    std::collections::HashMap,
    std::time::Instant,
};

#[cfg(target_os = "linux")]
use crate::linux::imp::{env_vars, heuristics};

#[cfg(not(target_os = "linux"))]
use crate::not_linux::imp::{env_vars, heuristics};

struct CacheItem<'a> {
    created: Instant,
    config: Arc<RwLock<Config<'a>>>,
}

type Cache = RwLock<HashMap<Option<String>, CacheItem<'static>>>;

lazy_static::lazy_static! {
    static ref ENV_VARS: HashMap<&'static str, Vec<&'static str>> = env_vars();
    static ref HEURISTICS: HashMap<&'static str, HeuristicFn> = heuristics();
    static ref CACHE: Cache = RwLock::new(HashMap::new());
}

fn is_cache_item_expired(item: &CacheItem<'static>, now: Instant) -> bool {
    now.checked_duration_since(item.created).map_or(false, |t| t > CONFIG_CACHE_TIMEOUT)
}

async fn read_cache(
    build_dir: &Option<String>,
    now: Instant,
    cache: &Cache,
) -> Option<Arc<RwLock<Config<'static>>>> {
    let read_guard = cache.read().await;
    (*read_guard)
        .get(build_dir)
        .filter(|item| !is_cache_item_expired(item, now))
        .map(|item| item.config.clone())
}

pub(crate) async fn load_config(
    build_dir: &Option<String>,
    ffx: Ffx,
    env: &Result<String>,
) -> Result<Arc<RwLock<Config<'static>>>> {
    load_config_with_instant(build_dir, Instant::now(), &CACHE, ffx, env).await
}

async fn load_config_with_instant(
    build_dir: &Option<String>,
    now: Instant,
    cache: &Cache,
    ffx: Ffx,
    env: &Result<String>,
) -> Result<Arc<RwLock<Config<'static>>>> {
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
                    (*write_guard).insert(
                        build_dir.as_ref().cloned(),
                        CacheItem {
                            created: now,
                            config: Arc::new(RwLock::new(Config::new(
                                &Environment::try_load(env.as_ref().ok()),
                                build_dir,
                                &ENV_VARS,
                                &HEURISTICS,
                                ffx,
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
    use {
        super::*,
        anyhow::bail,
        futures::future::join_all,
        std::{default::Default, time::Duration},
    };

    fn env() -> Result<String> {
        // Prevent any File I/O in unit tests.
        bail!("No environment when running tests")
    }

    async fn load(now: Instant, key: &Option<String>, cache: &Cache) {
        let tests = 25;
        let env = env();
        let mut futures = Vec::new();
        for _x in 0..tests {
            futures.push(load_config_with_instant(key, now, cache, Default::default(), &env));
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
                &Environment::try_load(env().as_ref().ok()),
                &build_dirs[0],
                &ENV_VARS,
                &HEURISTICS,
                Default::default(),
            )?)),
        };
        assert!(!is_cache_item_expired(&item, now));
        Ok(())
    }
}
