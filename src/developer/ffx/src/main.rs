// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    analytics::{add_crash_event, get_notice, opt_out_for_this_invocation},
    anyhow::{Context as _, Result},
    async_trait::async_trait,
    async_utils::async_once::Once,
    errors::{ffx_bail, ffx_error, FfxError, ResultExt as _},
    ffx_core::metrics::{add_ffx_launch_and_timing_events, init_metrics_svc},
    ffx_core::Injector,
    ffx_daemon::{get_daemon_proxy_single_link, is_daemon_running},
    ffx_lib_args::{from_env, redact_arg_values, Ffx},
    ffx_lib_sub_command::Subcommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::{
        DaemonError, DaemonProxy, FastbootMarker, FastbootProxy, TargetControlMarker,
        TargetControlProxy,
    },
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    fuchsia_async::TimeoutExt,
    futures::{Future, FutureExt},
    ring::digest::{Context as ShaContext, Digest, SHA256},
    std::default::Default,
    std::error::Error,
    std::fs::File,
    std::io::{BufReader, Read},
    std::path::PathBuf,
    std::time::{Duration, Instant},
};

// Config key for event timeout.
const PROXY_TIMEOUT_SECS: &str = "proxy.timeout_secs";

const CURRENT_EXE_HASH: &str = "current.hash";

struct Injection {
    daemon_once: Once<DaemonProxy>,
    remote_once: Once<RemoteControlProxy>,
}

impl Default for Injection {
    fn default() -> Self {
        Self { daemon_once: Once::new(), remote_once: Once::new() }
    }
}

impl Injection {
    async fn init_remote_proxy(&self) -> Result<RemoteControlProxy> {
        let daemon_proxy = self.daemon_factory().await?;
        let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
        let app: Ffx = argh::from_env();
        let target = app.target().await?;
        let result = daemon_proxy
            .get_remote_control(target.as_ref().map(|s| s.as_str()), remote_server_end)
            .on_timeout(proxy_timeout().await?, || Ok(Err(DaemonError::Timeout)))
            .await?;

        result
            .map_err(|err| FfxError::DaemonError {
                err,
                target,
                is_default_target: app.target.is_none(),
            })
            .map(|_| remote_proxy)
            .context("init_remote_proxy")
    }
}

#[async_trait]
impl Injector for Injection {
    // This could get called multiple times by the plugin system via multiple threads - so make sure
    // the spawning only happens one thread at a time.
    async fn daemon_factory(&self) -> Result<DaemonProxy> {
        self.daemon_once.get_or_try_init(init_daemon_proxy()).await.map(|proxy| proxy.clone())
    }

    async fn fastboot_factory(&self) -> Result<FastbootProxy> {
        let daemon_proxy = self.daemon_factory().await?;
        let (fastboot_proxy, fastboot_server_end) = create_proxy::<FastbootMarker>()?;
        let app: Ffx = argh::from_env();
        let target = app.target().await?;

        let result = daemon_proxy
            .get_fastboot(target.as_ref().map(|s| s.as_str()), fastboot_server_end)
            .on_timeout(proxy_timeout().await?, || Ok(Err(DaemonError::Timeout)))
            .await?;

        result
            .map_err(|err| match err {
                DaemonError::TargetNotFound => {
                    FfxError::FastbootError { target, is_default_target: app.target.is_none() }
                }
                _ => FfxError::DaemonError { err, target, is_default_target: app.target.is_none() },
            })
            .map(|_| fastboot_proxy)
            .context("fastboot_factory")
    }

    async fn target_factory(&self) -> Result<TargetControlProxy> {
        let daemon_proxy = self.daemon_factory().await?;
        let (target_proxy, target_server_end) = create_proxy::<TargetControlMarker>()?;
        let app: Ffx = argh::from_env();
        let target = app.target().await?;
        let result = daemon_proxy
            .get_target(target.as_ref().map(|s| s.as_str()), target_server_end)
            .on_timeout(proxy_timeout().await?, || Ok(Err(DaemonError::Timeout)))
            .await?;

        result
            .map_err(|err| FfxError::DaemonError {
                err,
                target,
                is_default_target: app.target.is_none(),
            })
            .map(|_| target_proxy)
            .context("target_factory")
    }

    async fn remote_factory(&self) -> Result<RemoteControlProxy> {
        self.remote_once.get_or_try_init(self.init_remote_proxy()).await.map(|proxy| proxy.clone())
    }

    async fn is_experiment(&self, key: &str) -> bool {
        ffx_config::get(key).await.unwrap_or(false)
    }
}

async fn init_daemon_proxy() -> Result<DaemonProxy> {
    if !is_daemon_running().await {
        #[cfg(not(test))]
        ffx_daemon::spawn_daemon().await?;
    }

    let (nodeid, proxy, link) = get_daemon_proxy_single_link(None).await?;

    // Spawn off the link task, so that FIDL functions can be called (link IO makes progress).
    let link_task = fuchsia_async::Task::local(link.map(|_| ()));

    // TODO(fxb/67400) Create an e2e test.
    #[cfg(test)]
    let hash: String = "testcurrenthash".to_owned();
    #[cfg(not(test))]
    let hash: String =
        match ffx_config::get((CURRENT_EXE_HASH, ffx_config::ConfigLevel::Runtime)).await {
            Ok(str) => str,
            Err(err) => {
                log::error!("BUG: ffx version information is missing! {:?}", err);
                link_task.detach();
                return Ok(proxy);
            }
        };

    let daemon_hash = timeout(proxy_timeout().await?, proxy.get_hash())
        .await
        .context("timeout")
        .map_err(|_| ffx_error!("ffx was unable to query the version of the running ffx daemon. Run `ffx doctor --restart-daemon` and try again."))?
        .context("Getting hash from daemon")?;
    if hash == daemon_hash {
        link_task.detach();
        return Ok(proxy);
    }

    log::info!("Daemon is a different version.  Attempting to restart");

    // Tell the daemon to quit, and wait for the link task to finish.
    // TODO(raggi): add a timeout on this, if the daemon quit fails for some
    // reason, the link task would hang indefinitely.
    let (quit_result, _) = futures::future::join(proxy.quit(), link_task).await;

    if !quit_result.is_ok() {
        ffx_bail!(
            "FFX daemon upgrade failed unexpectedly. \n\
            Try running `ffx doctor --restart-daemon` and then retrying your \
            command.\n\nError was: {:?}",
            quit_result
        )
    }

    #[cfg(not(test))]
    ffx_daemon::spawn_daemon().await?;

    let (_nodeid, proxy, link) = get_daemon_proxy_single_link(Some(vec![nodeid])).await?;

    fuchsia_async::Task::local(link.map(|_| ())).detach();

    Ok(proxy)
}

async fn proxy_timeout() -> Result<Duration> {
    let proxy_timeout: f64 = ffx_config::get(PROXY_TIMEOUT_SECS).await?;
    Ok(Duration::from_secs_f64(proxy_timeout))
}

#[derive(Debug)]
struct TimeoutError {}
impl std::fmt::Display for TimeoutError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "timed out")
    }
}
impl Error for TimeoutError {}

async fn timeout<F, T>(t: Duration, f: F) -> Result<T, TimeoutError>
where
    F: Future<Output = T> + Unpin,
{
    // TODO(raggi): this could be made more efficient (avoiding the box) with some additional work,
    // but for the local use cases here it's not sufficiently important.
    let mut timer = fuchsia_async::Timer::new(t).boxed().fuse();
    let mut f = f.fuse();

    futures::select! {
        _ = timer => Err(TimeoutError{}),
        res = f => Ok(res),
    }
}

fn is_daemon(subcommand: &Option<Subcommand>) -> bool {
    if let Some(Subcommand::FfxDaemonPlugin(ffx_daemon_plugin_args::DaemonCommand {
        subcommand: ffx_daemon_plugin_sub_command::Subcommand::FfxDaemonStart(_),
    })) = subcommand
    {
        return true;
    }
    false
}

fn set_hash_config(overrides: Option<String>) -> Result<Option<String>> {
    let input = std::env::current_exe()?;
    let reader = BufReader::new(File::open(input)?);
    let digest = sha256_digest(reader)?;

    let runtime = format!("{}={}", CURRENT_EXE_HASH, hex::encode(digest.as_ref()));
    match overrides {
        Some(s) => {
            if s.is_empty() {
                Ok(Some(runtime))
            } else {
                let new_overrides = format!("{},{}", s, runtime);
                Ok(Some(new_overrides))
            }
        }
        None => Ok(Some(runtime)),
    }
}

fn sha256_digest<R: Read>(mut reader: R) -> Result<Digest> {
    let mut context = ShaContext::new(&SHA256);
    let mut buffer = [0; 1024];

    loop {
        let count = reader.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        context.update(&buffer[..count]);
    }

    Ok(context.finish())
}

async fn run() -> Result<i32> {
    hoist::disable_autoconnect();
    let app: Ffx = from_env();

    // Configuration initialization must happen before ANY calls to the config (or the cache won't
    // properly have the runtime parameters.
    let overrides = set_hash_config(app.runtime_config_overrides())?;

    let env = match app.env {
        Some(ref env) => PathBuf::from(env),
        None => match ffx_config::default_env_path() {
            Ok(path) => path,
            Err(e) => {
                eprintln!(
                    "ffx could not determine the default environment configuration path: {}",
                    e
                );
                eprintln!("Ensure that $HOME is set, or pass the --env option to specify an environment configuration path");
                return Ok(1);
            }
        },
    };

    ffx_config::init(&*app.config, overrides, Some(env))?;
    ffx_config::logging::init(is_daemon(&app.subcommand)).await?;

    log::info!("starting command: {:?}", std::env::args().collect::<Vec<String>>());

    // HACK(64402): hoist uses a lazy static initializer obfuscating access to inject
    // this value by other means, so:
    let _ = ffx_config::get("overnet.socket").await.map(|sockpath: String| {
        std::env::set_var("ASCENDD", sockpath);
    });

    let analytics_disabled = ffx_config::get("ffx.analytics.disabled").await.unwrap_or(false);

    init_metrics_svc().await; // one time call to initialize app analytics
    if analytics_disabled {
        opt_out_for_this_invocation().await?
    }

    if let Some(note) = get_notice().await {
        eprintln!("{}", note);
    }

    let analytics_start = Instant::now();

    let command_start = Instant::now();
    let res = ffx_lib_suite::ffx_plugin_impl(Injection::default(), app).await;
    let command_done = Instant::now();
    log::info!("Command completed. Success: {}", res.is_ok());
    let command_duration = (command_done - command_start).as_secs_f32();
    let timing_in_millis = (command_done - command_start).as_millis().to_string();

    let analytics_task = fuchsia_async::Task::local(async move {
        let sanitized_args = redact_arg_values::<Ffx>();
        if let Err(e) = add_ffx_launch_and_timing_events(sanitized_args, timing_in_millis).await {
            log::error!("metrics submission failed: {}", e);
        }
        Instant::now()
    });

    let analytics_done = analytics_task
        // TODO(66918): make configurable, and evaluate chosen time value.
        .on_timeout(Duration::from_secs(2), || {
            log::error!("metrics submission timed out");
            // Metrics timeouts should not impact user flows.
            Instant::now()
        })
        .await;

    log::info!(
        "Run finished. success: {}, command time: {}, analytics time: {}",
        res.is_ok(),
        &command_duration,
        (analytics_done - analytics_start).as_secs_f32()
    );
    res
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let result = run().await;

    // unwrap because if stderr is not writable, the program should try to exit right away.
    errors::write_result(&result, &mut std::io::stderr()).unwrap();
    // Report BUG errors as crash events
    if result.is_err() && result.ffx_error().is_none() {
        let err_msg = format!("{}", result.as_ref().unwrap_err());
        // TODO(66918): make configurable, and evaluate chosen time value.
        if let Err(e) = add_crash_event(&err_msg, None)
            .on_timeout(Duration::from_secs(2), || {
                log::error!("analytics timed out reporting crash event");
                Ok(())
            })
            .await
        {
            log::error!("analytics failed to submit crash event: {}", e);
        }
    }

    std::process::exit(result.exit_code());
}

#[cfg(test)]
mod test {
    use super::*;
    use ascendd;
    use async_lock::Mutex;
    use async_net::unix::UnixListener;
    use fidl::endpoints::{ClientEnd, ProtocolMarker, RequestStream};
    use fidl_fuchsia_developer_bridge::{DaemonMarker, DaemonRequest, DaemonRequestStream};
    use fidl_fuchsia_overnet::{ServiceProviderRequest, ServiceProviderRequestStream};
    use fuchsia_async::Task;
    use futures::AsyncReadExt;
    use futures::TryStreamExt;
    use hoist::OvernetInstance;
    use std::path::PathBuf;
    use std::sync::Arc;
    use tempfile;

    fn setup_ascendd_temp() -> tempfile::TempPath {
        let path = tempfile::NamedTempFile::new().unwrap().into_temp_path();
        std::fs::remove_file(&path).unwrap();
        std::env::set_var("ASCENDD", &path);
        path
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_init_daemon_proxy_link_lost() {
        let sockpath = setup_ascendd_temp();

        // Start a listener that accepts and immediately closes the socket..
        let listener = UnixListener::bind(sockpath.to_owned()).unwrap();
        let _listen_task = Task::local(async move {
            loop {
                drop(listener.accept().await.unwrap());
            }
        });

        let res = init_daemon_proxy().await;
        let str = format!("{}", res.err().unwrap());
        assert!(str.contains("link lost"));
        assert!(str.contains("ffx doctor"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_init_daemon_proxy_timeout_no_connection() {
        let sockpath = setup_ascendd_temp();

        // Start a listener that never accepts the socket.
        let _listener = UnixListener::bind(sockpath.to_owned()).unwrap();

        let res = init_daemon_proxy().await;
        let str = format!("{}", res.err().unwrap());
        assert!(str.contains("Timed out"));
        assert!(str.contains("ffx doctor"));
    }

    async fn test_daemon(sockpath: PathBuf, hash: &str, sleep_secs: u64) {
        let daemon_hoist = Arc::new(hoist::Hoist::new().unwrap());

        let (s, p) = fidl::Channel::create().unwrap();
        daemon_hoist.publish_service(DaemonMarker::NAME, ClientEnd::new(p)).unwrap();

        let link_tasks = Arc::new(Mutex::new(Vec::<Task<()>>::new()));
        let link_tasks1 = link_tasks.clone();

        let listener = UnixListener::bind(sockpath.to_owned()).unwrap();
        let listen_task = Task::local(async move {
            // let (sock, _addr) = listener.accept().await.unwrap();
            let mut stream = listener.incoming();
            while let Some(sock) = stream.try_next().await.unwrap_or(None) {
                fuchsia_async::Timer::new(Duration::from_secs(sleep_secs)).await;
                let hoist_clone = daemon_hoist.clone();
                link_tasks1.lock().await.push(Task::local(async move {
                    let (mut rx, mut tx) = sock.split();
                    ascendd::run_stream(
                        hoist_clone.node(),
                        &mut rx,
                        &mut tx,
                        Some("fake daemon".to_string()),
                        None,
                    )
                    .map(|r| eprintln!("link error: {:?}", r))
                    .await;
                }));
            }
        });

        let mut stream = ServiceProviderRequestStream::from_channel(
            fidl::AsyncChannel::from_channel(s).unwrap(),
        );

        while let Some(ServiceProviderRequest::ConnectToService { chan, .. }) =
            stream.try_next().await.unwrap_or(None)
        {
            let link_tasks = link_tasks.clone();
            let mut stream =
                DaemonRequestStream::from_channel(fidl::AsyncChannel::from_channel(chan).unwrap());
            while let Some(request) = stream.try_next().await.unwrap_or(None) {
                match request {
                    DaemonRequest::GetHash { responder, .. } => responder.send(hash).unwrap(),
                    DaemonRequest::Quit { responder, .. } => {
                        std::fs::remove_file(sockpath).unwrap();
                        listen_task.cancel().await;
                        responder.send(true).unwrap();
                        // This is how long the daemon sleeps for, which
                        // is a workaround for the fact that we have no
                        // way to "flush" the response over overnet due
                        // to the constraints of mesh routing.
                        fuchsia_async::Timer::new(Duration::from_millis(20)).await;
                        link_tasks.lock().await.clear();
                        return;
                    }
                    _ => {
                        panic!("unimplemented stub for request: {:?}", request);
                    }
                }
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_init_daemon_proxy_hash_matches() {
        let sockpath = setup_ascendd_temp();

        let sockpath1 = sockpath.to_owned();
        let daemons_task = Task::local(async move {
            test_daemon(sockpath1.to_owned(), "testcurrenthash", 0).await;
        });

        // wait until daemon binds the socket path
        while std::fs::metadata(&sockpath).is_err() {
            fuchsia_async::Timer::new(Duration::from_millis(20)).await
        }

        let proxy = init_daemon_proxy().await.unwrap();
        proxy.quit().await.unwrap();
        daemons_task.await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_init_daemon_proxy_upgrade() {
        let sockpath = setup_ascendd_temp();

        // Spawn two daemons, the first out of date, the second is up to date.
        let sockpath1 = sockpath.to_owned();
        let daemons_task = Task::local(async move {
            test_daemon(sockpath1.to_owned(), "oldhash", 0).await;
            // Note: testcurrenthash is explicitly expected by #cfg in get_daemon_proxy
            test_daemon(sockpath1.to_owned(), "testcurrenthash", 0).await;
        });

        // wait until daemon binds the socket path
        while std::fs::metadata(&sockpath).is_err() {
            fuchsia_async::Timer::new(Duration::from_millis(20)).await
        }

        let proxy = init_daemon_proxy().await.unwrap();
        proxy.quit().await.unwrap();
        daemons_task.await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_init_daemon_blocked_for_4s_succeeds() {
        let sockpath = setup_ascendd_temp();

        // Spawn two daemons, the first out of date, the second is up to date.
        let sockpath1 = sockpath.to_owned();
        let daemon_task = Task::local(async move {
            test_daemon(sockpath1.to_owned(), "testcurrenthash", 4).await;
        });

        // wait until daemon binds the socket path
        while std::fs::metadata(&sockpath).is_err() {
            fuchsia_async::Timer::new(Duration::from_millis(20)).await
        }

        let proxy = init_daemon_proxy().await.unwrap();
        proxy.quit().await.unwrap();
        daemon_task.await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_init_daemon_blocked_for_6s_timesout() {
        let sockpath = setup_ascendd_temp();

        // Spawn two daemons, the first out of date, the second is up to date.
        let sockpath1 = sockpath.to_owned();
        let _daemon_task = Task::local(async move {
            test_daemon(sockpath1.to_owned(), "testcurrenthash", 6).await;
        });

        // wait until daemon binds the socket path
        while std::fs::metadata(&sockpath).is_err() {
            fuchsia_async::Timer::new(Duration::from_millis(20)).await
        }

        let err = init_daemon_proxy().await;
        assert!(err.is_err());
        let str = format!("{:?}", err);
        assert!(str.contains("Timed out"));
        assert!(str.contains("ffx doctor"));
    }
}
