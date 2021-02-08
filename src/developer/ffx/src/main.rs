// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    analytics::{add_crash_event, add_launch_event, show_analytics_notice},
    anyhow::{anyhow, Context, Result},
    ffx_core::{build_info, ffx_bail, ffx_error, FfxError},
    ffx_daemon::{find_and_connect, is_daemon_running, spawn_daemon},
    ffx_lib_args::{from_env, Ffx},
    ffx_lib_sub_command::Subcommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::{
        DaemonError, DaemonProxy, FastbootError, FastbootMarker, FastbootProxy,
    },
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    fuchsia_async::TimeoutExt,
    futures::Future,
    futures::FutureExt,
    lazy_static::lazy_static,
    ring::digest::{Context as ShaContext, Digest, SHA256},
    std::error::Error,
    std::fs::File,
    std::io::{BufReader, Read},
    std::sync::{Arc, Mutex},
    std::time::{Duration, Instant},
};

// app name for analytics
const APP_NAME: &str = "ffx";

// Config key for event timeout.
const PROXY_TIMEOUT_SECS: &str = "proxy.timeout_secs";
// TODO: a nice way to focus this error message would be to get a list of targets from the daemon
// and be able to distinguish whether there are in fact 0 or multiple available targets.
const TARGET_FAILURE_MSG: &str = "\
We weren't able to open a connection to a target.

Use `ffx target list` to verify the state of connected devices. This error
probably means that either:

1) There are no available targets. Make sure your device is connected.
2) There are multiple available targets and you haven't specified a target or
provided a default.
Tip: You can use `ffx --target \"my-nodename\" <command>` to specify a target
for a particular command, or use `ffx target default set \"my-nodename\"` if
you always want to use a particular target.";

const CURRENT_EXE_HASH: &str = "current.hash";

const NON_FASTBOOT_MSG: &str = "\
This command needs to be run against a target in the Fastboot state.
Try rebooting the device into Fastboot with the command `ffx target
reboot --bootloader` and try re-running this command.";

const TARGET_IN_FASTBOOT: &str = "\
This command cannot be run against a target in the Fastboot state. Try
rebooting the device or flashing the device into a running state.";

lazy_static! {
    // Using a mutex to guard the spawning of the daemon - the value it contains is not used.
    static ref SPAWN_GUARD: Arc<Mutex<bool>> = Arc::new(Mutex::new(false));
}

// This could get called multiple times by the plugin system via multiple threads - so make sure
// the spawning only happens one thread at a time.
async fn get_daemon_proxy() -> Result<DaemonProxy> {
    let mut check_hash = false;
    let _guard = SPAWN_GUARD.lock().unwrap();
    if !is_daemon_running().await {
        spawn_daemon().await?;
    } else {
        check_hash = true;
    }
    let proxy = find_and_connect(hoist::hoist()).await?;
    if check_hash {
        // TODO(fxb/67400) Create an e2e test.
        let hash: String =
            ffx_config::get((CURRENT_EXE_HASH, ffx_config::ConfigLevel::Runtime)).await?;
        let daemon_hash = proxy.get_hash().await?;
        if hash != daemon_hash {
            log::info!("Daemon is a different version.  Attempting to restart");
            if proxy.quit().await? {
                spawn_daemon().await?;
                return find_and_connect(hoist::hoist()).await;
            } else {
                ffx_bail!(
                    "FFX daemon is a different version. \n\
                    Try running `ffx doctor --force-daemon-restart` and then retrying your \
                    command"
                )
            }
        }
    }
    Ok(proxy)
}

async fn proxy_timeout() -> Result<Duration> {
    let proxy_timeout: ffx_config::Value = ffx_config::get(PROXY_TIMEOUT_SECS).await?;
    Ok(Duration::from_millis(
        (proxy_timeout
            .as_f64()
            .ok_or(anyhow!("unable to convert to float: {:?}", proxy_timeout))?
            * 1000.0) as u64,
    ))
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

async fn get_fastboot_proxy() -> Result<FastbootProxy> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (fastboot_proxy, fastboot_server_end) = create_proxy::<FastbootMarker>()?;
    let app: Ffx = argh::from_env();
    let result = timeout(
        proxy_timeout().await?,
        daemon_proxy
            .get_fastboot(app.target().await?.as_ref().map(|s| s.as_str()), fastboot_server_end),
    )
    .await
    .context("timeout")?
    .context("connecting to Fastboot")?;

    match result {
        Ok(_) => Ok(fastboot_proxy),
        Err(FastbootError::NonFastbootDevice) => Err(ffx_error!(NON_FASTBOOT_MSG).into()),
        Err(e) => Err(anyhow!("unexpected failure connecting to Fastboot: {:?}", e)),
    }
}

async fn get_remote_proxy() -> Result<RemoteControlProxy> {
    let daemon_proxy = get_daemon_proxy().await?;
    let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
    let app: Ffx = argh::from_env();

    let result = timeout(
        proxy_timeout().await?,
        daemon_proxy.get_remote_control(
            app.target().await?.as_ref().map(|s| s.as_str()),
            remote_server_end,
        ),
    )
    .await
    .context("timeout")?
    .context("connecting to daemon")?;

    match result {
        Ok(_) => Ok(remote_proxy),
        Err(DaemonError::TargetCacheError) => Err(ffx_error!(TARGET_FAILURE_MSG).into()),
        Err(DaemonError::TargetInFastboot) => Err(ffx_error!(TARGET_IN_FASTBOOT).into()),
        Err(e) => Err(anyhow!("unexpected failure connecting to RCS: {:?}", e)),
    }
}

async fn is_experiment_subcommand_on(key: &'static str) -> bool {
    ffx_config::get(key).await.unwrap_or(false)
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

async fn run() -> Result<()> {
    let app: Ffx = from_env();

    // Configuration initialization must happen before ANY calls to the config (or the cache won't
    // properly have the runtime parameters.
    let overrides = set_hash_config(app.runtime_config_overrides())?;
    ffx_config::init_config(&app.config, &overrides, &app.env)?;
    let log_to_stdio = app.verbose || is_daemon(&app.subcommand);
    ffx_config::logging::init(log_to_stdio).await?;

    log::info!("starting command: {:?}", std::env::args().collect::<Vec<String>>());

    // HACK(64402): hoist uses a lazy static initializer obfuscating access to inject
    // this value by other means, so:
    let _ = ffx_config::get("overnet.socket").await.map(|sockpath: String| {
        std::env::set_var("ASCENDD", sockpath);
    });

    let notice_writer = Box::new(std::io::stderr());
    show_analytics_notice(notice_writer);

    let analytics_start = Instant::now();

    let analytics_task = fuchsia_async::Task::spawn(async {
        let args: Vec<String> = std::env::args().collect();
        // drop arg[0]: executable with hard path
        // TODO do we want to break out subcommands for analytics?
        let args_str = &args[1..].join(" ");
        let launch_args = format!("{}", &args_str);
        let build_info = build_info();
        let build_version = build_info.build_version;
        if let Err(e) =
            add_launch_event(APP_NAME, build_version.as_deref(), Some(launch_args).as_deref()).await
        {
            log::error!("analytics submission failed: {}", e);
        }
        Instant::now()
    });

    let command_start = Instant::now();
    let res = ffx_lib_suite::ffx_plugin_impl(
        get_daemon_proxy,
        get_remote_proxy,
        get_fastboot_proxy,
        is_experiment_subcommand_on,
        app,
    )
    .await;
    let command_done = Instant::now();
    log::info!("Command completed. Success: {}", res.is_ok());

    let analytics_done = analytics_task
        // TODO(66918): make configurable, and evaluate chosen time value.
        .on_timeout(Duration::from_secs(2), || {
            log::error!("analytics submission timed out");
            // Analytics timeouts should not impact user flows
            Instant::now()
        })
        .await;

    log::info!(
        "Run finished. success: {}, command time: {}, analytics time: {}",
        res.is_ok(),
        (command_done - command_start).as_secs_f32(),
        (analytics_done - analytics_start).as_secs_f32()
    );
    res
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    match run().await {
        Ok(_) => {
            // TODO add event for timing here at end
            std::process::exit(0)
        }
        Err(err) => {
            let error_code = if let Some(ffx_err) = err.downcast_ref::<FfxError>() {
                eprintln!("{}", ffx_err);
                match ffx_err {
                    FfxError::Error(_, code) => *code,
                }
            } else {
                eprintln!("BUG: An internal command error occurred.\n{:?}", err);
                1
            };
            let err_msg = format!("{}", err);
            // TODO(66918): make configurable, and evaluate chosen time value.
            if let Err(e) = add_crash_event(&err_msg)
                .on_timeout(Duration::from_secs(2), || {
                    log::error!("analytics timed out reporting crash event");
                    Ok(())
                })
                .await
            {
                log::error!("analytics failed to submit crash event: {}", e);
            }
            std::process::exit(error_code);
        }
    }
}
