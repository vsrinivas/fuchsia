// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::Config,
    crate::environment::Environment,
    crate::heuristic_config::HeuristicFn,
    anyhow::{anyhow, Error},
    async_std::sync::{Arc, RwLock},
    ffx_core::constants::CONFIG_CACHE_TIMEOUT,
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
    env: &Result<String, Error>,
) -> Result<Arc<RwLock<Config<'static>>>, Error> {
    load_config_with_instant(build_dir, Instant::now(), &CACHE, ffx, env).await
}

async fn load_config_with_instant(
    build_dir: &Option<String>,
    now: Instant,
    cache: &Cache,
    ffx: Ffx,
    env: &Result<String, Error>,
) -> Result<Arc<RwLock<Config<'static>>>, Error> {
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
                .ok_or_else(|| anyhow!("could not get config from cache"))
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use ffx_core::args::DaemonCommand;
    use ffx_lib_sub_command::Subcommand;
    use futures::future::join_all;
    use std::time::Duration;

    fn env() -> Result<String, Error> {
        // Prevent any File I/O in unit tests.
        Err(anyhow!("test no environment"))
    }

    fn cli() -> Ffx {
        Ffx { config: None, subcommand: Subcommand::Daemon(DaemonCommand {}) }
    }

    async fn load(now: Instant, key: &Option<String>, cache: &Cache) {
        let tests = 25;
        let env = env();
        let mut futures = Vec::new();
        for _x in 0..tests {
            futures.push(load_config_with_instant(key, now, cache, cli(), &env));
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

    #[test]
    fn test_config_one_at_a_time() {
        let tests = 10;
        let (now, build_dirs, cache) = setup(tests);
        hoist::run(async move {
            for x in 0..tests {
                load_and_test(now, x, x + 1, &build_dirs[x], &cache).await;
            }
        });
    }

    #[test]
    fn test_config_many_at_a_time() {
        let tests = 25;
        let (now, build_dirs, cache) = setup(tests);
        hoist::run(async move {
            let futures = build_dirs.iter().map(|x| load(now, &x, &cache));
            let result = join_all(futures).await;
            assert_eq!(tests, result.len());
            {
                let read_guard = cache.read().await;
                assert_eq!(tests, (*read_guard).len());
            }
        });
    }

    #[test]
    fn test_config_timeout() {
        let tests = 1;
        let (now, build_dirs, cache) = setup(tests);
        hoist::run(async move {
            load_and_test(now, 0, 1, &build_dirs[0], &cache).await;
            let timeout =
                now.checked_add(CONFIG_CACHE_TIMEOUT).expect("timeout should not overflow");
            let after_timeout = timeout
                .checked_add(Duration::from_millis(1))
                .expect("after timeout should not overflow");
            load_and_test(timeout, 1, 1, &build_dirs[0], &cache).await;
            load_and_test(after_timeout, 1, 1, &build_dirs[0], &cache).await;
        });
    }

    #[test]
    fn test_expiration_check_does_not_panic() -> Result<(), Error> {
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
                cli(),
            )?)),
        };
        assert!(!is_cache_item_expired(&item, now));
        Ok(())
    }
}
