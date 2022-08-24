// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    analytics::{add_crash_event, get_notice, opt_out_for_this_invocation},
    anyhow::{Context as _, Result},
    async_trait::async_trait,
    async_utils::async_once::Once,
    buildid,
    errors::{ffx_bail, ffx_error, FfxError, ResultExt as _},
    ffx_config::get_log_dirs,
    ffx_core::Injector,
    ffx_daemon::{get_daemon_proxy_single_link, is_daemon_running},
    ffx_lib_args::{from_env, redact_arg_values, Ffx},
    ffx_lib_sub_command::SubCommand,
    ffx_metrics::{add_ffx_launch_and_timing_events, init_metrics_svc},
    ffx_target::{get_remote_proxy, open_target_with_fut},
    ffx_writer::Writer,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_ffx::{
        DaemonError, DaemonProxy, FastbootMarker, FastbootProxy, TargetProxy, VersionInfo,
    },
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fuchsia_async::TimeoutExt,
    futures::FutureExt,
    std::default::Default,
    std::fs::File,
    std::io::Write,
    std::path::PathBuf,
    std::str::FromStr,
    std::time::{Duration, Instant},
    timeout::timeout,
};

// Config key for event timeout.
const PROXY_TIMEOUT_SECS: &str = "proxy.timeout_secs";

const CURRENT_EXE_BUILDID: &str = "current.buildid";

fn is_default_target() -> bool {
    let app: Ffx = argh::from_env();
    app.target.is_none()
}

struct Injection {
    daemon_once: Once<DaemonProxy>,
    remote_once: Once<RemoteControlProxy>,
    target: Once<Option<String>>,
}

impl std::fmt::Debug for Injection {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Injection").finish()
    }
}

impl Default for Injection {
    fn default() -> Self {
        Self { target: Once::new(), daemon_once: Once::new(), remote_once: Once::new() }
    }
}

impl Injection {
    async fn target(&self) -> Result<Option<String>> {
        self.target
            .get_or_try_init(async {
                let app: Ffx = argh::from_env();
                app.target().await
            })
            .await
            .map(|s| s.clone())
    }

    #[tracing::instrument(level = "info")]
    async fn init_remote_proxy(&self) -> Result<RemoteControlProxy> {
        let daemon_proxy = self.daemon_factory().await?;
        let target = self.target().await?;
        let proxy_timeout = proxy_timeout().await?;
        get_remote_proxy(target, is_default_target(), daemon_proxy, proxy_timeout).await
    }

    async fn fastboot_factory_inner(&self) -> Result<FastbootProxy> {
        let daemon_proxy = self.daemon_factory().await?;
        let target = self.target().await?;
        let (target_proxy, target_proxy_fut) = open_target_with_fut(
            target,
            is_default_target(),
            daemon_proxy.clone(),
            proxy_timeout().await?,
        )?;
        target_proxy_fut.await?;
        let (fastboot_proxy, fastboot_server_end) = create_proxy::<FastbootMarker>()?;
        target_proxy.open_fastboot(fastboot_server_end)?;
        Ok(fastboot_proxy)
    }

    async fn target_factory_inner(&self) -> Result<TargetProxy> {
        let target = self.target().await?;
        let daemon_proxy = self.daemon_factory().await?;
        let (target_proxy, target_proxy_fut) = open_target_with_fut(
            target,
            is_default_target(),
            daemon_proxy.clone(),
            proxy_timeout().await?,
        )?;
        target_proxy_fut.await?;
        Ok(target_proxy)
    }

    async fn daemon_timeout_error(&self) -> Result<FfxError> {
        Ok(FfxError::DaemonError {
            err: DaemonError::Timeout,
            target: self.target().await?,
            is_default_target: is_default_target(),
        })
    }
}

#[async_trait(?Send)]
impl Injector for Injection {
    // This could get called multiple times by the plugin system via multiple threads - so make sure
    // the spawning only happens one thread at a time.
    #[tracing::instrument(level = "info")]
    async fn daemon_factory(&self) -> Result<DaemonProxy> {
        self.daemon_once.get_or_try_init(init_daemon_proxy()).await.map(|proxy| proxy.clone())
    }

    async fn fastboot_factory(&self) -> Result<FastbootProxy> {
        let target = self.target().await?;
        let timeout_error = self.daemon_timeout_error().await?;
        timeout(proxy_timeout().await?, self.fastboot_factory_inner()).await.map_err(|_| {
            tracing::warn!("Timed out getting fastboot proxy for: {:?}", target);
            timeout_error
        })?
    }

    async fn target_factory(&self) -> Result<TargetProxy> {
        let target = self.target().await?;
        let timeout_error = self.daemon_timeout_error().await?;
        timeout(proxy_timeout().await?, self.target_factory_inner()).await.map_err(|_| {
            tracing::warn!("Timed out getting fastboot proxy for: {:?}", target);
            timeout_error
        })?
    }

    #[tracing::instrument(level = "info")]
    async fn remote_factory(&self) -> Result<RemoteControlProxy> {
        let target = self.target().await?;
        let timeout_error = self.daemon_timeout_error().await?;
        timeout(proxy_timeout().await?, async {
            self.remote_once
                .get_or_try_init(self.init_remote_proxy())
                .await
                .map(|proxy| proxy.clone())
        })
        .await
        .map_err(|_| {
            tracing::warn!("Timed out getting remote control proxy for: {:?}", target);
            timeout_error
        })?
    }

    async fn is_experiment(&self, key: &str) -> bool {
        ffx_config::get(key).await.unwrap_or(false)
    }

    async fn build_info(&self) -> Result<VersionInfo> {
        Ok::<VersionInfo, anyhow::Error>(ffx_build_version::build_info())
    }

    async fn writer(&self) -> Result<Writer> {
        let app: Ffx = argh::from_env();
        Ok(Writer::new(app.machine))
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
    let build_id: String = "testcurrenthash".to_owned();
    #[cfg(not(test))]
    let build_id: String = {
        let build_id = ffx_config::query(CURRENT_EXE_BUILDID)
            .level(Some(ffx_config::ConfigLevel::Runtime))
            .get()
            .await;
        match build_id {
            Ok(str) => str,
            Err(err) => {
                tracing::error!("BUG: ffx version information is missing! {:?}", err);
                link_task.detach();
                return Ok(proxy);
            }
        }
    };

    let daemon_version_info = timeout(proxy_timeout().await?, proxy.get_version_info())
        .await
        .context("timeout")
        .map_err(|_| {
            ffx_error!(
                "ffx was unable to query the version of the running ffx daemon. \
                                 Run `ffx doctor --restart-daemon` and try again."
            )
        })?
        .context("Getting hash from daemon")?;

    if Some(build_id) == daemon_version_info.build_id {
        link_task.detach();

        match daemon_version_info.exec_path {
            None => tracing::warn!("Daemon version info did not contain an executable path."),
            Some(daemon_path) => {
                let path = std::env::current_exe().map(|x| x.to_string_lossy().to_string()).ok();

                if let Some(path) = path {
                    let canonical_path = std::fs::canonicalize(path.clone())?;
                    let canonical_daemon_path = std::fs::canonicalize(daemon_path.clone())?;
                    if canonical_path != canonical_daemon_path {
                        eprintln!(
                            "Warning: Found a running daemon ({}) that is from a different copy of ffx.",
                        daemon_path);
                        tracing::warn!(
                            "Found a running daemon that is from a different copy of ffx. \
                                Continuing to connect...\
                                \n\nDaemon path: {}\
                                \nffx front-end path: {}",
                            daemon_path,
                            path
                        );
                    }
                } else {
                    tracing::warn!("Could not get path of ffx executable");
                }
            }
        };

        return Ok(proxy);
    }

    eprintln!("Daemon is a different version, attempting to restart");
    tracing::info!("Daemon is a different version, attempting to restart");

    // Tell the daemon to quit, and wait for the link task to finish.
    // TODO(raggi): add a timeout on this, if the daemon quit fails for some
    // reason, the link task would hang indefinitely.
    let (quit_result, _) = futures::future::join(proxy.quit(), link_task).await;

    if !quit_result.is_ok() {
        ffx_bail!(
            "ffx daemon upgrade failed unexpectedly. \n\
            Try running `ffx doctor --restart-daemon` and then retry your \
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

fn is_daemon(subcommand: &Option<SubCommand>) -> bool {
    if let Some(SubCommand::FfxDaemonPlugin(ffx_daemon_plugin_args::DaemonCommand {
        subcommand: ffx_daemon_plugin_sub_command::SubCommand::FfxDaemonStart(_),
    })) = subcommand
    {
        return true;
    }
    false
}

fn is_schema(subcommand: &Option<SubCommand>) -> bool {
    matches!(subcommand, Some(SubCommand::FfxSchema(_)))
}

fn set_buildid_config(overrides: Option<String>) -> Result<Option<String>> {
    let runtime = format!("{}={}", CURRENT_EXE_BUILDID, buildid::get_build_id()?);
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

async fn report_log_hint(writer: &mut dyn std::io::Write) {
    let msg = if let Ok(log_dirs) = get_log_dirs().await {
        if log_dirs.len() == 1 {
            format!(
                "More information may be available in ffx host logs in directory:\n    {}",
                log_dirs[0]
            )
        } else {
            format!(
                "More information may be available in ffx host logs in directories:\n    {}",
                log_dirs.join("\n    ")
            )
        }
    } else {
        "More information may be available in ffx host logs, but ffx failed to retrieve configured log file locations".to_string()
    };
    if writeln!(writer, "{}", msg).is_err() {
        println!("{}", msg);
    }
}

fn stamp_file(stamp: &Option<String>) -> Result<Option<File>> {
    if let Some(stamp) = stamp {
        Ok(Some(File::create(stamp)?))
    } else {
        Ok(None)
    }
}

fn write_exit_code<T, W: Write>(res: &Result<T>, out: &mut W) -> Result<()> {
    write!(out, "{}\n", res.exit_code())?;
    Ok(())
}

async fn run() -> Result<()> {
    let app: Ffx = from_env();

    // Configuration initialization must happen before ANY calls to the config (or the cache won't
    // properly have the runtime parameters.
    let overrides = set_buildid_config(app.runtime_config_overrides())?;

    let env =
        ffx_config::init(&*app.config, overrides, app.env.as_ref().map(PathBuf::from)).await?;

    match env.env_path() {
        Ok(path) => path,
        Err(e) => {
            eprintln!("ffx could not determine the environment configuration path: {}", e);
            eprintln!("Ensure that $HOME is set, or pass the --env option to specify an environment configuration path");
            return Ok(());
        }
    };

    let is_daemon = is_daemon(&app.subcommand);
    ffx_config::logging::init(is_daemon || app.verbose, !is_daemon).await?;

    tracing::info!("starting command: {:?}", std::env::args().collect::<Vec<String>>());

    // Since this is invoking the config, this must be run _after_ ffx_config::init.
    let log_level = app.log_level().await?;
    let _:  simplelog::LevelFilter = simplelog::LevelFilter::from_str(log_level.as_str()).with_context(||
        ffx_error!("'{}' is not a valid log level. Supported log levels are 'Off', 'Error', 'Warn', 'Info', 'Debug', and 'Trace'", log_level))?;

    let analytics_disabled = ffx_config::get("ffx.analytics.disabled").await.unwrap_or(false);
    let ffx_invoker = ffx_config::get("fuchsia.analytics.ffx_invoker").await.unwrap_or(None);
    let injection = Injection::default();
    init_metrics_svc(injection.build_info().await?, ffx_invoker).await; // one time call to initialize app analytics
    if analytics_disabled {
        opt_out_for_this_invocation().await?
    }

    if let Some(note) = get_notice().await {
        eprintln!("{}", note);
    }

    let analytics_start = Instant::now();

    let command_start = Instant::now();

    let stamp = stamp_file(&app.stamp)?;
    let res = if is_schema(&app.subcommand) {
        ffx_lib_suite::ffx_plugin_writer_all_output(0);
        Ok(())
    } else if app.machine.is_some() && !ffx_lib_suite::ffx_plugin_is_machine_supported(&app) {
        Err(anyhow::Error::new(ffx_error!("The machine flag is not supported for this subcommand")))
    } else {
        ffx_lib_suite::ffx_plugin_impl(&injection, app).await
    };

    let command_done = Instant::now();
    tracing::info!("Command completed. Success: {}", res.is_ok());
    let command_duration = (command_done - command_start).as_secs_f32();
    let timing_in_millis = (command_done - command_start).as_millis().to_string();

    let analytics_task = fuchsia_async::Task::local(async move {
        let sanitized_args = redact_arg_values();
        if let Err(e) = add_ffx_launch_and_timing_events(sanitized_args, timing_in_millis).await {
            tracing::error!("metrics submission failed: {}", e);
        }
        Instant::now()
    });

    let analytics_done = analytics_task
        // TODO(66918): make configurable, and evaluate chosen time value.
        .on_timeout(Duration::from_secs(2), || {
            tracing::error!("metrics submission timed out");
            // Metrics timeouts should not impact user flows.
            Instant::now()
        })
        .await;

    tracing::info!(
        "Run finished. success: {}, command time: {}, analytics time: {}",
        res.is_ok(),
        &command_duration,
        (analytics_done - analytics_start).as_secs_f32()
    );

    // Write to our stamp file if it was requested
    if let Some(mut stamp) = stamp {
        write_exit_code(&res, &mut stamp)?;
        stamp.sync_all()?;
    }

    res
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let result = run().await;
    let mut stderr = std::io::stderr();

    // unwrap because if stderr is not writable, the program should try to exit right away.
    errors::write_result(&result, &mut stderr).unwrap();
    // Report BUG errors as crash events
    if result.is_err() && result.ffx_error().is_none() {
        let err_msg = format!("{}", result.as_ref().unwrap_err());
        // TODO(66918): make configurable, and evaluate chosen time value.
        if let Err(e) = add_crash_event(&err_msg, None)
            .on_timeout(Duration::from_secs(2), || {
                tracing::error!("analytics timed out reporting crash event");
                Ok(())
            })
            .await
        {
            tracing::error!("analytics failed to submit crash event: {}", e);
        }
        report_log_hint(&mut stderr).await;
    }

    std::process::exit(result.exit_code());
}

#[cfg(test)]
mod test {
    use super::*;
    use ascendd;
    use async_lock::Mutex;
    use async_net::unix::UnixListener;
    use fidl::endpoints::{ClientEnd, DiscoverableProtocolMarker, RequestStream};
    use fidl_fuchsia_developer_ffx::{DaemonMarker, DaemonRequest, DaemonRequestStream};
    use fidl_fuchsia_overnet::{ServiceProviderRequest, ServiceProviderRequestStream};
    use fuchsia_async::Task;
    use futures::AsyncReadExt;
    use futures::TryStreamExt;
    use hoist::OvernetInstance;
    use std::io::BufWriter;
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

    async fn test_daemon(sockpath: PathBuf, build_id: &str, sleep_secs: u64) {
        let version_info = VersionInfo {
            exec_path: Some(std::env::current_exe().unwrap().to_string_lossy().to_string()),
            build_id: Some(build_id.to_owned()),
            ..VersionInfo::EMPTY
        };
        let daemon_hoist = Arc::new(hoist::Hoist::new().unwrap());
        let _t = hoist::Hoist::start_socket_link(sockpath.to_str().unwrap().to_string());

        let (s, p) = fidl::Channel::create().unwrap();
        daemon_hoist.publish_service(DaemonMarker::PROTOCOL_NAME, ClientEnd::new(p)).unwrap();

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
                    DaemonRequest::GetVersionInfo { responder, .. } => {
                        responder.send(version_info.clone()).unwrap()
                    }
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
        let _env = ffx_config::test_init().await.unwrap();
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
        let _env = ffx_config::test_init().await.unwrap();
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
        let _env = ffx_config::test_init().await.unwrap();
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
        let _env = ffx_config::test_init().await.unwrap();
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stamp_file_creation() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("stamp").into_os_string().into_string().ok();
        let stamp = stamp_file(&path);

        assert!(stamp.unwrap().is_some());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stamp_file_no_create() {
        let no_stamp = stamp_file(&None);
        assert!(no_stamp.unwrap().is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_write_exit_code() {
        let mut out = BufWriter::new(Vec::new());
        write_exit_code(&Ok(0), &mut out).unwrap();
        assert_eq!(String::from_utf8(out.into_inner().unwrap()).unwrap(), "0\n");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_write_exit_code_on_failure() {
        let mut out = BufWriter::new(Vec::new());
        write_exit_code(&Result::<()>::Err(anyhow::anyhow!("fail")), &mut out).unwrap();
        assert_eq!(String::from_utf8(out.into_inner().unwrap()).unwrap(), "1\n")
    }
}
