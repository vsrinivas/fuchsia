// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::*,
    anyhow::{anyhow, Context, Error, Result},
    async_lock::Mutex,
    async_trait::async_trait,
    doctor_utils::{DaemonManager, DefaultDaemonManager, DoctorRecorder, Recorder},
    errors::ffx_bail,
    ffx_config::{get, print_config},
    ffx_core::ffx_plugin,
    ffx_doctor_args::DoctorCommand,
    fidl::endpoints::create_proxy,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_bridge::{
        DaemonProxy, TargetCollectionMarker, TargetCollectionProxy, TargetCollectionReaderMarker,
        TargetCollectionReaderRequest, TargetInfo, TargetMarker, TargetQuery, TargetState,
        VersionInfo,
    },
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    futures::TryStreamExt,
    itertools::Itertools,
    serde_json::json,
    std::sync::Arc,
    std::{
        collections::HashMap,
        io::{stdout, BufWriter, Write},
        path::PathBuf,
        process::Command,
        time::Duration,
    },
    termion::{color, style},
    timeout::timeout,
};

mod constants;

const DEFAULT_TARGET_CONFIG: &str = "target.default";
const DOCTOR_OUTPUT_FILENAME: &str = "doctor_output.txt";
const PLATFORM_INFO_FILENAME: &str = "platform.json";
const USER_CONFIG_FILENAME: &str = "user_config.txt";
const RECORD_CONFIG_SETTING: &str = "doctor.record_config";

macro_rules! success_or_continue {
    ($fut:expr, $handler:ident, $v:ident, $e:expr $(,)?) => {
        match $fut.await {
            Ok(Ok(s)) => {
                $handler.result(StepResult::Success).await?;
                let $v = s;
                $e
            }

            Ok(Err(e)) => {
                $handler.result(StepResult::Error(e.into())).await?;
                continue;
            }
            Err(_) => {
                $handler.result(StepResult::Timeout).await?;
                continue;
            }
        }
    };
}

#[derive(Debug, PartialEq)]
enum StepType {
    Started(Result<Option<String>, String>, Option<String>),
    AttemptStarted(usize, usize),
    DaemonForceRestart,
    DaemonRunning,
    KillingZombieDaemons,
    SpawningDaemon,
    ConnectingToDaemon,
    CommunicatingWithDaemon,
    DaemonVersion(VersionInfo),
    ListingTargets(String),
    DaemonChecksFailed,
    NoTargetsFound,
    TerminalNoTargetsFound,
    SkippedFastboot(Option<String>),
    SkippedZedboot(Option<String>),
    CheckingTarget(Option<String>),
    RcsAttemptStarted(usize, usize),
    OpeningTargetHandle(Option<String>),
    ConnectingToRcs,
    CommunicatingWithRcs,
    TargetSummary(HashMap<TargetCheckResult, Vec<Option<String>>>),
    RcsTerminalFailure,
    GeneratingRecord,
    RecordGenerated(PathBuf),
}

#[derive(Debug)]
enum StepResult {
    Success,
    Timeout,
    Error(Error),
    Other(String),
}

impl std::fmt::Display for StepResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = match self {
            StepResult::Success => SUCCESS.to_string(),
            StepResult::Timeout => FAILED_TIMEOUT.to_string(),
            StepResult::Error(e) => format_err(e.into()),
            StepResult::Other(s) => s.to_string(),
        };

        write!(f, "{}", s)
    }
}

impl StepType {
    fn with_bug_link(&self, s: &str) -> String {
        let mut s = String::from(s);
        s.push_str(&format!("\nFile a bug at: {}", BUG_URL));
        s
    }
}

impl std::fmt::Display for StepType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = match self {
            StepType::Started(default_target, version_str) => {
                let default_str = match default_target {
                    Ok(t) => {
                        if t.is_none() || t.as_ref().unwrap().is_empty() {
                            "(none set)".to_string()
                        } else {
                            t.clone().unwrap()
                        }
                    }
                    Err(e) => format!("config read failed: {:?}", e),
                };
                let welcome_str = format!(
                    "\nWelcome to ffx doctor.\n- Frontend version: {}\n- Default target: {}\n",
                    version_str.as_ref().unwrap_or(&"Unknown".to_string()),
                    default_str
                );
                format!("{}{}\n{}{}", style::Bold, welcome_str, DAEMON_CHECK_INTRO, style::Reset)
            }
            StepType::AttemptStarted(i, total) => {
                format!("\n\nKilling the daemon and trying again. Attempt {} of {}", i + 1, total)
            }
            StepType::KillingZombieDaemons => KILLING_ZOMBIE_DAEMONS.to_string(),
            StepType::DaemonForceRestart => FORCE_DAEMON_RESTART_MESSAGE.to_string(),
            StepType::DaemonRunning => DAEMON_RUNNING_CHECK.to_string(),
            StepType::SpawningDaemon => SPAWNING_DAEMON.to_string(),
            StepType::ConnectingToDaemon => CONNECTING_TO_DAEMON.to_string(),
            StepType::CommunicatingWithDaemon => COMMUNICATING_WITH_DAEMON.to_string(),
            StepType::DaemonVersion(version_info) => format!(
                "{}Daemon version: {}{}",
                style::Bold,
                version_info.build_version.as_ref().unwrap_or(&"Unknown".to_string()),
                style::Reset,
            ),
            StepType::ListingTargets(filter) => {
                if filter.is_empty() {
                    String::from(LISTING_TARGETS_NO_FILTER)
                } else {
                    format!("Attempting to list targets with filter '{}'...", filter)
                }
            }
            StepType::NoTargetsFound => NO_TARGETS_FOUND_SHORT.to_string(),
            StepType::DaemonChecksFailed => self.with_bug_link(&format!(
                "\n{}{}{}{}",
                style::Bold,
                color::Fg(color::Red),
                DAEMON_CHECKS_FAILED,
                style::Reset
            )),
            StepType::TerminalNoTargetsFound => {
                self.with_bug_link(&format!("\n{}", NO_TARGETS_FOUND_EXTENDED))
            }
            StepType::SkippedFastboot(nodename) => format!(
                "{}\nSkipping target in fastboot: '{}'. {}{}",
                style::Bold,
                nodename.as_ref().unwrap_or(&"UNKNOWN".to_string()),
                TARGET_CHOICE_HELP,
                style::Reset
            ),
            StepType::SkippedZedboot(nodename) => format!(
                "{}\nSkipping target in zedboot: '{}'. {}{}",
                style::Bold,
                nodename.as_ref().unwrap_or(&"UNKNOWN".to_string()),
                TARGET_CHOICE_HELP,
                style::Reset
            ),
            StepType::CheckingTarget(nodename) => format!(
                "{}\nChecking target: '{}'. {}{}",
                style::Bold,
                nodename.as_deref().unwrap_or("UNKNOWN"),
                TARGET_CHOICE_HELP,
                style::Reset
            ),
            StepType::RcsAttemptStarted(attempt_num, retry_count) => {
                format!("\n\nAttempt {} of {}", attempt_num + 1, retry_count)
            }
            StepType::OpeningTargetHandle(nodename) => format!(
                "{}\nOpening target handle for: '{}'. {}{}",
                style::Bold,
                nodename.as_deref().unwrap_or("UNKNOWN"),
                TARGET_CHOICE_HELP,
                style::Reset,
            ),
            StepType::ConnectingToRcs => CONNECTING_TO_RCS.to_string(),
            StepType::CommunicatingWithRcs => COMMUNICATING_WITH_RCS.to_string(),
            StepType::TargetSummary(results) => {
                let mut s = format!("\n\n{}{}\n", style::Bold, TARGET_SUMMARY,);
                let keys = results.keys().into_iter().sorted_by_key(|k| format!("{:?}", k));
                for key in keys {
                    for nodename_opt in results.get(key).unwrap_or(&vec![]).iter() {
                        let nodename = nodename_opt.clone().unwrap_or("UNKNOWN".to_string());
                        match *key {
                            TargetCheckResult::Success => {
                                s.push_str(&format!("{}✓ {}\n", color::Fg(color::Green), nodename));
                            }
                            TargetCheckResult::Failed => {
                                s.push_str(&format!("{}✗ {}\n", color::Fg(color::Red), nodename));
                            }
                            TargetCheckResult::SkippedFastboot => {
                                s.push_str(&format!("skipped: {}\n", nodename));
                            }
                            TargetCheckResult::SkippedZedboot => {
                                s.push_str(&format!("skipped: {}\n", nodename));
                            }
                        }
                    }
                }
                s.push_str(&format!("{}", style::Reset));
                s
            }
            StepType::RcsTerminalFailure => self.with_bug_link(&format!(
                "{}\n{}",
                RCS_TERMINAL_FAILURE, RCS_TERMINAL_FAILURE_BUG_INSTRUCTIONS
            )),
            StepType::GeneratingRecord => "Generating record...".to_string(),
            StepType::RecordGenerated(path) => {
                format!("Record generated at: {}", path.to_string_lossy().into_owned())
            }
        };

        write!(f, "{}", s)
    }
}

#[async_trait]
trait DoctorStepHandler {
    async fn step(&mut self, step: StepType) -> Result<()>;
    async fn output_step(&mut self, step: StepType) -> Result<()>;
    async fn result(&mut self, result: StepResult) -> Result<()>;
}

struct DefaultDoctorStepHandler {
    recorder: Arc<Mutex<dyn Recorder + Send>>,
    writer: Box<dyn Write + Send + Sync>,
}

#[async_trait]
// The StepHandler interface exists to provide a clean separation between the
// imperative implementation logic and the output of each step. It has the added
// benefit of clearly indicating what each block of `doctor` is attempting to do.
impl DoctorStepHandler for DefaultDoctorStepHandler {
    // This is a logical step which will have a result. Right now the only difference
    // between it and an output_step is the addition of a newline after the step content.
    async fn step(&mut self, step: StepType) -> Result<()> {
        write!(&mut self.writer, "{}", step)?;
        self.writer.flush()?;
        let mut r = self.recorder.lock().await;
        r.add_content(DOCTOR_OUTPUT_FILENAME, format!("{}", step));
        Ok(())
    }

    // This is step which exists merely to provide output (such as an introduction or
    // result summary).
    async fn output_step(&mut self, step: StepType) -> Result<()> {
        writeln!(&mut self.writer, "{}", step)?;
        let mut r = self.recorder.lock().await;
        r.add_content(DOCTOR_OUTPUT_FILENAME, format!("{}\n", step));
        Ok(())
    }

    // This represents the result of a `step`.
    async fn result(&mut self, result: StepResult) -> Result<()> {
        writeln!(&mut self.writer, "{}", result)?;
        let mut r = self.recorder.lock().await;
        r.add_content(DOCTOR_OUTPUT_FILENAME, format!("{}\n", result));
        Ok(())
    }
}

impl DefaultDoctorStepHandler {
    fn new(
        recorder: Arc<Mutex<dyn Recorder + Send>>,
        writer: Box<dyn Write + Send + Sync>,
    ) -> Self {
        Self { recorder, writer }
    }
}

async fn get_config_permission<W: Write>(mut writer: W) -> Result<bool> {
    match get(RECORD_CONFIG_SETTING).await {
        Ok(true) => {
            writeln!(
                &mut writer,
                "Config recording is enabled - config data will be recorded. You can change this \
                     with `ffx config set doctor.record_config false"
            )?;
            return Ok(true);
        }
        Ok(false) => {
            writeln!(
                &mut writer,
                "Config recording is disabled - config data will not be recorded. You can change \
                     this with `ffx config set doctor.record_config true"
            )?;
            return Ok(false);
        }
        _ => (),
    }

    let permission: bool;
    loop {
        let mut input = String::new();
        writeln!(&mut writer, "Do you want to include your config data `ffx config get`? [y/n]")?;
        // TODO(fxbug.dev/81228) Use a generic read type instead of stdin
        std::io::stdin().read_line(&mut input)?;
        permission = match input.to_lowercase().trim() {
            "yes" | "y" => true,
            "no" | "n" => false,
            _ => continue,
        };
        break;
    }

    writeln!(
        &mut writer,
        "You can permanently enable or disable including config data in doctor records with:"
    )?;
    writeln!(&mut writer, "`ffx config set {} [true|false]`", RECORD_CONFIG_SETTING)?;
    fuchsia_async::Timer::new(Duration::from_millis(1000)).await;

    Ok(permission)
}

fn format_err(e: &Error) -> String {
    format!("{}\n\t{:?}", FAILED_WITH_ERROR, e)
}

struct DoctorRecorderParameters {
    record: bool,
    user_config_enabled: bool,
    log_root: Option<PathBuf>,
    output_dir: Option<PathBuf>,
    recorder: Arc<Mutex<dyn Recorder>>,
}

#[derive(Debug, PartialEq, Eq, Hash)]
enum TargetCheckResult {
    Success,
    Failed,
    SkippedFastboot,
    SkippedZedboot,
}

#[ffx_plugin()]
pub async fn doctor_cmd(build_info: VersionInfo, cmd: DoctorCommand) -> Result<()> {
    doctor_cmd_impl(build_info, cmd, stdout()).await
}

pub async fn doctor_cmd_impl<W: Write + Send + Sync + 'static>(
    build_info: VersionInfo,
    cmd: DoctorCommand,
    mut writer: W,
) -> Result<()> {
    let daemon_manager = DefaultDaemonManager {};
    let delay = Duration::from_millis(cmd.retry_delay);

    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let target_str = ffx.target.unwrap_or(String::default());

    let mut log_root = None;
    let mut output_dir = None;
    let mut record = cmd.record;
    match get("log.enabled").await {
        Ok(enabled) => {
            let enabled: bool = enabled;
            if !enabled && cmd.record {
                writeln!(&mut writer,
                    "{}WARNING:{} --record was provided but ffx logs are not enabled. This means your record will only include doctor output.",
                    color::Fg(color::Red), style::Reset
                )?;
                writeln!(&mut writer,
                    "ffx doctor will proceed, but if you want to enable logs, you can do so by running:"
                )?;
                writeln!(&mut writer, "  ffx config set log.enabled true")?;
                writeln!(&mut writer, "You will then need to restart the ffx daemon:")?;
                writeln!(&mut writer, "  ffx doctor --force-restart\n\n")?;
                fuchsia_async::Timer::new(Duration::from_millis(10000)).await;
            }

            log_root = Some(get("log.dir").await?);
            let final_output_dir =
                cmd.output_dir.map(|s| PathBuf::from(s)).unwrap_or(std::env::current_dir()?);

            if !final_output_dir.is_dir() {
                ffx_bail!(
                    "cannot record: output directory does not exist or is unreadable: {:?}",
                    output_dir
                );
            }

            output_dir = Some(final_output_dir);
        }
        Err(e) => {
            writeln!(
                &mut writer,
                "{}WARNING:{} getting log status from ffx config failed. The error was: {:?}",
                color::Fg(color::Red),
                style::Reset,
                e
            )?;
            if cmd.record {
                writeln!(
                    &mut writer,
                    "Record mode requires configuration and will be turned off for this run."
                )?;
            }
            writeln!(&mut writer, "If this issue persists, please file a bug here: {}", BUG_URL)?;
            fuchsia_async::Timer::new(Duration::from_millis(10000)).await;

            record = false;
        }
    };

    let user_config_enabled = if !record || cmd.no_config {
        false
    } else {
        match get_config_permission(&mut writer).await {
            Ok(b) => b,
            Err(e) => {
                writeln!(&mut writer, "Failed to get permission to record config data: {}", e)?;
                writeln!(&mut writer, "Config data will not be recorded")?;
                false
            }
        }
    };

    let recorder = Arc::new(Mutex::new(DoctorRecorder::new()));
    let mut handler = DefaultDoctorStepHandler::new(recorder.clone(), Box::new(writer));
    let default_target = get(DEFAULT_TARGET_CONFIG)
        .await
        .map_err(|e: ffx_config::api::ConfigError| format!("{:?}", e).replace("\n", ""));

    doctor(
        &mut handler,
        &daemon_manager,
        &target_str,
        cmd.retry_count,
        delay,
        cmd.restart_daemon,
        build_info.build_version,
        default_target,
        DoctorRecorderParameters {
            record,
            user_config_enabled,
            log_root,
            output_dir,
            recorder: recorder.clone(),
        },
    )
    .await?;

    Ok(())
}

fn get_kernel_name() -> Result<String> {
    Ok(String::from_utf8(Command::new("uname").output()?.stdout)?)
}

async fn list_targets(query: Option<&str>, tc: &TargetCollectionProxy) -> Result<Vec<TargetInfo>> {
    let (reader, server) = fidl::endpoints::create_endpoints::<TargetCollectionReaderMarker>()?;

    tc.list_targets(
        TargetQuery { string_matcher: query.map(|s| s.to_owned()), ..TargetQuery::EMPTY },
        reader,
    )?;
    let mut res = Vec::new();
    let mut stream = server.into_stream()?;
    while let Ok(Some(TargetCollectionReaderRequest::Next { entry, responder })) =
        stream.try_next().await
    {
        responder.send()?;
        if entry.len() > 0 {
            res.extend(entry);
        } else {
            break;
        }
    }
    Ok(res)
}

fn get_platform_info() -> Result<String> {
    let kernel_name = match get_kernel_name() {
        Ok(s) => s,
        Err(e) => format!("Could not get kernel name: {}", e),
    };

    let platform_info = json!({
        "kernel_name": kernel_name.replace("\n",""),
    });

    Ok(serde_json::to_string_pretty(&platform_info)?)
}

async fn get_user_config() -> Result<String> {
    let mut writer = BufWriter::new(Vec::new());
    print_config(&mut writer, &None).await?;
    let config_str = String::from_utf8(writer.into_inner()?)?;
    Ok(config_str)
}

async fn doctor(
    step_handler: &mut impl DoctorStepHandler,
    daemon_manager: &impl DaemonManager,
    target_str: &str,
    retry_count: usize,
    retry_delay: Duration,
    restart_daemon: bool,
    build_version_string: Option<String>,
    default_target: Result<Option<String>, String>,
    record_params: DoctorRecorderParameters,
) -> Result<()> {
    execute_steps(
        step_handler,
        daemon_manager,
        target_str,
        retry_count,
        retry_delay,
        restart_daemon,
        build_version_string,
        default_target,
    )
    .await?;

    if record_params.record {
        let log_root =
            record_params.log_root.context("log_root not present despite record set to true")?;
        let output_dir = record_params
            .output_dir
            .context("output_dir not present despite record set to true")?;

        let mut daemon_log = log_root.clone();
        daemon_log.push("ffx.daemon.log");
        let mut fe_log = log_root.clone();
        fe_log.push("ffx.log");

        step_handler.step(StepType::GeneratingRecord).await?;

        let platform_info = match get_platform_info() {
            Ok(s) => s,
            Err(e) => format!("Could not serialize platform info: {}", e),
        };

        let final_path = {
            let mut r = record_params.recorder.lock().await;
            r.add_sources(vec![daemon_log, fe_log]);
            r.add_content(PLATFORM_INFO_FILENAME, platform_info);

            if record_params.user_config_enabled {
                let config_str = match get_user_config().await {
                    Ok(s) => s,
                    Err(e) => format!("Could not get config data output: {}", e),
                };
                r.add_content(USER_CONFIG_FILENAME, config_str);
            }

            match r.generate(output_dir.clone()) {
                Ok(p) => p,
                Err(e) => {
                    let path = &output_dir.to_str().unwrap_or("path undefined");
                    let advice = "You can change the output directory for the generated zip file \
                                  using `--output-dir`.";
                    let default_err_msg =
                        Err(anyhow!("{}\nCould not write to: {}\n{}", e, &path, advice));

                    match e.downcast_ref::<zip::result::ZipError>() {
                        Some(zip::result::ZipError::Io(io_error)) => {
                            match io_error.raw_os_error() {
                                Some(27) => Err(anyhow!(
                                    "{}\nMake sure you can write files larger than 1MB to: {}\n{}",
                                    e,
                                    &path,
                                    advice
                                ))?,
                                _ => default_err_msg?,
                            }
                        }
                        _ => default_err_msg?,
                    }
                }
            }
        };

        step_handler.result(StepResult::Success).await?;
        step_handler.output_step(StepType::RecordGenerated(final_path.canonicalize()?)).await?;
    }
    Ok(())
}

async fn execute_steps(
    step_handler: &mut impl DoctorStepHandler,
    daemon_manager: &impl DaemonManager,
    target_str: &str,
    retry_count: usize,
    retry_delay: Duration,
    restart_daemon: bool,
    build_version_string: Option<String>,
    default_target: Result<Option<String>, String>,
) -> Result<()> {
    step_handler.output_step(StepType::Started(default_target, build_version_string)).await?;

    let mut proxy_opt: Option<DaemonProxy> = None;
    let mut targets_opt: Option<Vec<TargetInfo>> = None;
    let mut tc_proxy_opt: Option<TargetCollectionProxy> = None;
    for i in 0..retry_count {
        proxy_opt = None;
        if i > 0 {
            daemon_manager.kill_all().await?;
            step_handler.output_step(StepType::AttemptStarted(i, retry_count)).await?;
        } else if restart_daemon {
            step_handler.output_step(StepType::DaemonForceRestart).await?;
            daemon_manager.kill_all().await?;
        }

        step_handler.step(StepType::DaemonRunning).await?;
        if !daemon_manager.is_running().await {
            step_handler.result(StepResult::Other(NONE_RUNNING.to_string())).await?;
            step_handler.step(StepType::KillingZombieDaemons).await?;

            if daemon_manager.kill_all().await? {
                step_handler.result(StepResult::Other(ZOMBIE_KILLED.to_string())).await?;
            } else {
                step_handler.result(StepResult::Other(NONE_RUNNING.to_string())).await?;
            }

            step_handler.step(StepType::SpawningDaemon).await?;

            // HACK: Wait a few seconds before spawning a new daemon. Attempting
            // to spawn one too quickly after killing one will lead to timeouts
            // when attempting to communicate with the spawned daemon.
            // Temporary fix for fxbug.dev/66958. Remove when that bug is resolved.
            fuchsia_async::Timer::new(Duration::from_millis(5000)).await;
            success_or_continue!(timeout(retry_delay, daemon_manager.spawn()), step_handler, _p, {
            });
        } else {
            step_handler.result(StepResult::Other(FOUND.to_string())).await?;
        }

        step_handler.step(StepType::ConnectingToDaemon).await?;
        proxy_opt = success_or_continue!(
            timeout(retry_delay, daemon_manager.find_and_connect()),
            step_handler,
            p,
            Some(p)
        );

        step_handler.step(StepType::CommunicatingWithDaemon).await?;
        match timeout(retry_delay, proxy_opt.as_ref().unwrap().get_version_info()).await {
            Err(_) => {
                step_handler.result(StepResult::Timeout).await?;
                proxy_opt = None;
                continue;
            }
            Ok(Err(e)) => {
                step_handler.result(StepResult::Error(e.into())).await?;
                proxy_opt = None;
                continue;
            }
            Ok(Ok(v)) => {
                step_handler.result(StepResult::Success).await?;
                step_handler.output_step(StepType::DaemonVersion(v)).await?;
            }
        };

        let (tc_proxy, tc_server) = fidl::endpoints::create_proxy::<TargetCollectionMarker>()?;
        success_or_continue!(
            timeout(
                retry_delay,
                proxy_opt
                    .as_ref()
                    .unwrap()
                    .connect_to_protocol(TargetCollectionMarker::NAME, tc_server.into_channel())
            ),
            step_handler,
            _t,
            {},
        );
        step_handler.step(StepType::ListingTargets(target_str.to_string())).await?;
        targets_opt = success_or_continue!(
            timeout(retry_delay, list_targets(Some(target_str), &tc_proxy)),
            step_handler,
            t,
            Some(t),
        );
        tc_proxy_opt.replace(tc_proxy);

        if targets_opt.is_some() && targets_opt.as_ref().unwrap().len() == 0 {
            step_handler.output_step(StepType::NoTargetsFound).await?;
            continue;
        }

        break;
    }

    if proxy_opt.is_none() {
        step_handler.output_step(StepType::DaemonChecksFailed).await?;
        return Ok(());
    }

    if targets_opt.is_none() || targets_opt.as_ref().unwrap().len() == 0 {
        step_handler.output_step(StepType::TerminalNoTargetsFound).await?;
        return Ok(());
    }

    let targets = targets_opt.unwrap();
    let mut target_results: HashMap<Option<String>, TargetCheckResult> = HashMap::new();
    let tc_proxy = tc_proxy_opt.unwrap();

    for target in targets.iter() {
        // Note: this match statement intentionally does not have a fallback case in order to ensure
        // that behavior is considered when we add a new state.
        match target.target_state {
            None => {}
            Some(TargetState::Unknown) => {}
            Some(TargetState::Disconnected) => {}
            Some(TargetState::Product) => {}
            Some(TargetState::Fastboot) => {
                target_results.insert(target.nodename.clone(), TargetCheckResult::SkippedFastboot);
                step_handler
                    .output_step(StepType::SkippedFastboot(target.nodename.clone()))
                    .await?;
                continue;
            }
            Some(TargetState::Zedboot) => {
                target_results.insert(target.nodename.clone(), TargetCheckResult::SkippedZedboot);
                step_handler.output_step(StepType::SkippedZedboot(target.nodename.clone())).await?;
                continue;
            }
        }
        step_handler.output_step(StepType::CheckingTarget(target.nodename.clone())).await?;
        for i in 0..retry_count {
            if i > 0 {
                step_handler.output_step(StepType::RcsAttemptStarted(i, retry_count)).await?;
            }

            // TODO(jwing): SSH into the device and kill Overnet+RCS if anything below this fails
            let (target_proxy, target_server) = fidl::endpoints::create_proxy::<TargetMarker>()?;
            step_handler.step(StepType::OpeningTargetHandle(target.nodename.clone())).await?;
            success_or_continue!(
                timeout(
                    retry_delay,
                    tc_proxy.open_target(
                        TargetQuery {
                            string_matcher: target.nodename.clone(),
                            ..TargetQuery::EMPTY
                        },
                        target_server
                    )
                ),
                step_handler,
                _t,
                {},
            );
            step_handler.step(StepType::ConnectingToRcs).await?;
            let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
            success_or_continue!(
                timeout(retry_delay, target_proxy.open_remote_control(remote_server_end)),
                step_handler,
                _p,
                {},
            );

            step_handler.step(StepType::CommunicatingWithRcs).await?;

            success_or_continue!(
                timeout(retry_delay, remote_proxy.identify_host()),
                step_handler,
                _t,
                {
                    target_results.insert(target.nodename.clone(), TargetCheckResult::Success);
                }
            );
            break;
        }

        if target_results.get(&target.nodename).is_none() {
            target_results.insert(target.nodename.clone(), TargetCheckResult::Failed);
        }
    }

    let grouped_map = target_results.into_iter().map(|(k, v)| (v, k)).into_group_map();
    let has_failure = grouped_map.get(&TargetCheckResult::Failed).is_some();

    step_handler.output_step(StepType::TargetSummary(grouped_map)).await?;

    if has_failure {
        step_handler.output_step(StepType::RcsTerminalFailure).await?;
    }

    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        async_lock::Mutex,
        async_trait::async_trait,
        fidl::endpoints::RequestStream,
        fidl::endpoints::{spawn_local_stream_handler, ProtocolMarker, Request, ServerEnd},
        fidl::Channel,
        fidl_fuchsia_developer_bridge::{
            DaemonRequest, OpenTargetError, RemoteControlState, TargetCollectionRequest,
            TargetCollectionRequestStream, TargetRequest, TargetType,
        },
        fidl_fuchsia_developer_remotecontrol::{
            IdentifyHostResponse, RemoteControlMarker, RemoteControlRequest,
        },
        fuchsia_async as fasync,
        futures::channel::oneshot::{self, Receiver},
        futures::future::Shared,
        futures::{Future, FutureExt, TryFutureExt},
        std::cell::Cell,
        std::collections::HashSet,
        std::sync::Arc,
        tempfile::tempdir,
    };

    const NODENAME: &str = "fake-nodename";
    const UNRESPONSIVE_NODENAME: &str = "fake-nodename-unresponsive";
    const FASTBOOT_NODENAME: &str = "fastboot-nodename-unresponsive";
    const NON_EXISTENT_NODENAME: &str = "extra-fake-nodename";
    const DEFAULT_RETRY_DELAY: Duration = Duration::from_millis(2000);
    const DAEMON_VERSION_STR: &str = "daemon-build-string";

    #[derive(PartialEq)]
    struct TestStep {
        step_type: StepType,
        output_only: bool,
    }

    impl std::fmt::Debug for TestStep {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            let output_str = if self.output_only { " (output)" } else { "" };

            write!(f, "{:?}{}", self.step_type, output_str)
        }
    }

    struct TestStepEntry {
        step: Option<TestStep>,
        result: Option<StepResult>,
    }

    impl TestStepEntry {
        fn step(step_type: StepType) -> Self {
            Self { step: Some(TestStep { step_type, output_only: false }), result: None }
        }

        fn output_step(step_type: StepType) -> Self {
            Self { step: Some(TestStep { step_type, output_only: true }), result: None }
        }

        fn result(result: StepResult) -> Self {
            Self { result: Some(result), step: None }
        }
    }

    impl std::fmt::Debug for TestStepEntry {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            if self.step.is_some() {
                write!(f, "{:?}", self.step.as_ref().unwrap())
            } else if self.result.is_some() {
                write!(f, "{:?}", self.result.as_ref().unwrap())
            } else {
                panic!("attempted to debug TestStepEntry with empty step and result")
            }
        }
    }

    impl PartialEq for TestStepEntry {
        fn eq(&self, other: &Self) -> bool {
            if self.step != other.step {
                return false;
            }

            match (self.result.as_ref(), other.result.as_ref()) {
                (Some(r), Some(r2)) => match (r, r2) {
                    (StepResult::Error(_), StepResult::Error(_)) => true,
                    (StepResult::Other(s), StepResult::Other(s2)) => s == s2,
                    (StepResult::Success, StepResult::Success) => true,
                    (StepResult::Timeout, StepResult::Timeout) => true,
                    _ => false,
                },
                (None, None) => true,
                _ => false,
            }
        }
    }

    struct FakeStepHandler {
        steps: Arc<Mutex<Vec<TestStepEntry>>>,
    }
    impl FakeStepHandler {
        fn new() -> Self {
            Self { steps: Arc::new(Mutex::new(Vec::new())) }
        }

        async fn assert_matches_steps(&self, expected_steps: Vec<TestStepEntry>) {
            let steps = self.steps.lock().await;
            if *steps != expected_steps {
                println!("got: {:#?}\nexpected: {:#?}", steps, expected_steps);

                for (step, expected) in steps.iter().zip(expected_steps) {
                    if *step != expected {
                        println!("different step: got: {:?}, expected: {:?}", step, expected)
                    }
                }
                panic!("steps didn't match. differences are listed above.");
            }
        }
    }

    #[async_trait]
    impl DoctorStepHandler for FakeStepHandler {
        async fn step(&mut self, step: StepType) -> Result<()> {
            let mut v = self.steps.lock().await;
            v.push(TestStepEntry::step(step));
            Ok(())
        }

        async fn output_step(&mut self, step: StepType) -> Result<()> {
            let mut v = self.steps.lock().await;
            v.push(TestStepEntry::output_step(step));
            Ok(())
        }

        async fn result(&mut self, result: StepResult) -> Result<()> {
            let mut v = self.steps.lock().await;
            v.push(TestStepEntry::result(result));
            Ok(())
        }
    }

    struct FakeRecorder {
        expected_sources: Vec<PathBuf>,
        expected_output_dir: PathBuf,
        generate_called: Cell<bool>,
    }

    impl FakeRecorder {
        fn new(expected_sources: Vec<PathBuf>, expected_output_dir: PathBuf) -> Self {
            return Self {
                expected_sources,
                expected_output_dir,
                generate_called: Cell::new(false),
            };
        }

        fn assert_generate_called(&self) {
            assert!(self.generate_called.get())
        }

        fn result_path() -> PathBuf {
            PathBuf::from("/tmp").canonicalize().unwrap()
        }
    }

    impl Recorder for FakeRecorder {
        fn add_sources(&mut self, sources: Vec<PathBuf>) {
            let source_set: HashSet<_> = sources.iter().collect();
            let expected_set: HashSet<_> = self.expected_sources.iter().collect();
            assert_eq!(source_set, expected_set);
        }

        fn add_content(&mut self, _filename: &str, _content: String) {
            // Do nothing, we don't verify output in tests.
        }

        fn generate(&self, output_dir: PathBuf) -> Result<PathBuf> {
            assert_eq!(output_dir, self.expected_output_dir);
            self.generate_called.set(true);
            Ok(Self::result_path())
        }
    }
    struct DisabledRecorder {}

    impl DisabledRecorder {
        fn new() -> Self {
            return Self {};
        }
    }

    impl Recorder for DisabledRecorder {
        fn add_sources(&mut self, _sources: Vec<PathBuf>) {
            panic!("add_sources should not be called.")
        }

        fn add_content(&mut self, _filename: &str, _content: String) {
            // Do nothing, we don't verify output in tests.
        }

        fn generate(&self, _output_dir: PathBuf) -> Result<PathBuf> {
            panic!("generate should not be called.")
        }
    }

    struct FakeStateManager {
        kill_results: Vec<Result<bool>>,
        daemons_running_results: Vec<bool>,
        spawn_results: Vec<Result<()>>,
        find_and_connect_results: Vec<Result<DaemonProxy>>,
    }

    struct FakeDaemonManager {
        state_manager: Arc<Mutex<FakeStateManager>>,
    }

    impl FakeDaemonManager {
        fn new(
            daemons_running_results: Vec<bool>,
            kill_results: Vec<Result<bool>>,
            spawn_results: Vec<Result<()>>,
            find_and_connect_results: Vec<Result<DaemonProxy>>,
        ) -> Self {
            return FakeDaemonManager {
                state_manager: Arc::new(Mutex::new(FakeStateManager {
                    kill_results,
                    daemons_running_results,
                    spawn_results,
                    find_and_connect_results,
                })),
            };
        }

        async fn assert_no_leftover_calls(&self) {
            let state = self.state_manager.lock().await;
            assert!(
                state.kill_results.is_empty(),
                "too few calls to kill_all. remaining entries: {:?}",
                state.kill_results
            );
            assert!(
                state.daemons_running_results.is_empty(),
                "too few calls to is_running. remaining entries: {:?}",
                state.daemons_running_results
            );
            assert!(
                state.spawn_results.is_empty(),
                "too few calls to spawn. remaining entries: {:?}",
                state.spawn_results
            );
            assert!(
                state.find_and_connect_results.is_empty(),
                "too few calls to find_and_connect. remaining entries: {:?}",
                state.find_and_connect_results
            );
        }
    }

    #[async_trait]
    impl DaemonManager for FakeDaemonManager {
        async fn kill_all(&self) -> Result<bool> {
            let mut state = self.state_manager.lock().await;
            assert!(!state.kill_results.is_empty(), "too many calls to kill_all");
            state.kill_results.remove(0)
        }

        // placeholder method
        async fn get_pid(&self) -> Result<Vec<usize>> {
            Ok(Vec::new())
        }

        async fn is_running(&self) -> bool {
            let mut state = self.state_manager.lock().await;
            assert!(!state.daemons_running_results.is_empty(), "too many calls to is_running");
            state.daemons_running_results.remove(0)
        }

        async fn spawn(&self) -> Result<()> {
            let mut state = self.state_manager.lock().await;
            assert!(!state.spawn_results.is_empty(), "too many calls to spawn");
            state.spawn_results.remove(0)
        }

        async fn find_and_connect(&self) -> Result<DaemonProxy> {
            let mut state = self.state_manager.lock().await;
            assert!(
                !state.find_and_connect_results.is_empty(),
                "too many calls to find_and_connect"
            );
            state.find_and_connect_results.remove(0)
        }
    }

    fn serve_stream<T, F, Fut>(stream: T::RequestStream, mut f: F)
    where
        T: ProtocolMarker,
        F: FnMut(Request<T>) -> Fut + 'static + std::marker::Send,
        Fut: Future<Output = ()> + 'static + std::marker::Send,
    {
        fasync::Task::local(
            stream
                .try_for_each(move |r| f(r).map(Ok))
                .unwrap_or_else(|e| panic!("failed to handle request: {:?}", e)),
        )
        .detach();
    }

    // Spawns a target collection, accepting closures for handling listing and opening target hanles.
    fn spawn_target_collection<F, F2>(
        server_channel: Channel,
        list_closure: F,
        open_targets_closure: F2,
    ) where
        F: Fn(TargetQuery) -> Vec<TargetInfo> + Clone + 'static,
        F2: Fn(TargetQuery, ServerEnd<TargetMarker>) -> Result<(), OpenTargetError>
            + Clone
            + 'static,
    {
        let channel = fidl::AsyncChannel::from_channel(server_channel).unwrap();
        let mut stream = TargetCollectionRequestStream::from_channel(channel);
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    TargetCollectionRequest::ListTargets { query, reader, .. } => {
                        let reader = reader.into_proxy().unwrap();
                        let list_closure = list_closure.clone();
                        let results = (list_closure)(query);
                        if !results.is_empty() {
                            reader.next(&mut results.into_iter()).await.unwrap();
                            reader.next(&mut vec![].into_iter()).await.unwrap();
                        } else {
                            reader.next(&mut vec![].into_iter()).await.unwrap();
                        }
                    }
                    TargetCollectionRequest::OpenTarget { query, responder, target_handle } => {
                        let mut res = (open_targets_closure)(query, target_handle);
                        responder.send(&mut res).unwrap();
                    }
                    _ => {}
                }
            }
        })
        .detach();
    }

    fn spawn_target_handler<F>(target_handle: ServerEnd<TargetMarker>, handler: F)
    where
        F: Fn(TargetRequest) -> () + 'static,
    {
        fuchsia_async::Task::local(async move {
            let mut stream = target_handle.into_stream().unwrap();
            while let Ok(Some(req)) = stream.try_next().await {
                (handler)(req)
            }
        })
        .detach();
    }

    fn setup_responsive_daemon_server() -> DaemonProxy {
        spawn_local_stream_handler(move |req| async move {
            match req {
                DaemonRequest::GetVersionInfo { responder } => {
                    responder.send(daemon_version_info()).unwrap();
                }
                DaemonRequest::ConnectToProtocol { responder, name: _, server_end } => {
                    spawn_target_collection(
                        server_end,
                        |_| vec![],
                        |_query, target_handle| {
                            spawn_target_handler(target_handle, |req| match req {
                                TargetRequest::OpenRemoteControl {
                                    responder,
                                    remote_control: _,
                                } => {
                                    responder.send().unwrap();
                                }
                                r => panic!("unexpected request: {:?}", r),
                            });
                            Ok(())
                        },
                    );
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => {
                    assert!(false, "got unexpected request: {:?}", req);
                }
            }
        })
        .unwrap()
    }

    fn serve_responsive_rcs(server_end: ServerEnd<RemoteControlMarker>) {
        serve_stream::<RemoteControlMarker, _, _>(
            server_end.into_stream().unwrap(),
            move |req| async move {
                match req {
                    RemoteControlRequest::IdentifyHost { responder } => responder
                        .send(&mut Ok(IdentifyHostResponse {
                            addresses: Some(vec![]),
                            nodename: Some(NODENAME.to_string()),
                            ..IdentifyHostResponse::EMPTY
                        }))
                        .unwrap(),
                    _ => panic!("Unexpected request: {:?}", req),
                }
            },
        );
    }
    fn serve_unresponsive_rcs(
        server_end: ServerEnd<RemoteControlMarker>,
        waiter: Shared<Receiver<()>>,
    ) {
        serve_stream::<RemoteControlMarker, _, _>(server_end.into_stream().unwrap(), move |req| {
            let waiter = waiter.clone();
            async move {
                match req {
                    RemoteControlRequest::IdentifyHost { responder: _ } => {
                        waiter.await.unwrap();
                    }
                    _ => panic!("Unexpected request: {:?}", req),
                }
            }
        });
    }

    fn setup_responsive_daemon_server_with_fastboot_target() -> DaemonProxy {
        spawn_local_stream_handler(move |req| async move {
            match req {
                DaemonRequest::GetVersionInfo { responder } => {
                    responder.send(daemon_version_info()).unwrap();
                }
                DaemonRequest::ConnectToProtocol { name: _, server_end, responder } => {
                    spawn_target_collection(
                        server_end,
                        |_| {
                            vec![TargetInfo {
                                nodename: Some(FASTBOOT_NODENAME.to_string()),
                                addresses: Some(vec![]),
                                age_ms: Some(0),
                                rcs_state: Some(RemoteControlState::Unknown),
                                target_type: Some(TargetType::Unknown),
                                target_state: Some(TargetState::Fastboot),
                                ..TargetInfo::EMPTY
                            }]
                        },
                        |_query, target_handle| {
                            spawn_target_handler(target_handle, |req| match req {
                                TargetRequest::OpenRemoteControl { responder, remote_control } => {
                                    serve_responsive_rcs(remote_control);
                                    responder.send().unwrap();
                                }
                                r => panic!("unexpected request: {:?}", r),
                            });
                            Ok(())
                        },
                    );
                    responder.send(&mut Ok(())).unwrap();
                }
                req => {
                    assert!(false, "got unexpected request: {:?}", req);
                }
            }
        })
        .unwrap()
    }

    fn setup_responsive_daemon_server_with_targets(
        has_nodename: bool,
        waiter: Shared<Receiver<()>>,
    ) -> DaemonProxy {
        spawn_local_stream_handler(move |req| {
            let waiter = waiter.clone();
            async move {
                let nodename = if has_nodename { Some(NODENAME.to_string()) } else { None };
                match req {
                    DaemonRequest::GetVersionInfo { responder } => {
                        responder.send(daemon_version_info()).unwrap();
                    }
                    DaemonRequest::ConnectToProtocol { name: _, server_end, responder } => {
                        let nodename = nodename.clone();
                        let waiter = waiter.clone();
                        spawn_target_collection(
                            server_end,
                            move |query| {
                                let query = query.string_matcher.as_deref().unwrap_or("");
                                if !query.is_empty()
                                    && query != NODENAME
                                    && query != UNRESPONSIVE_NODENAME
                                {
                                    vec![]
                                } else if query == NODENAME {
                                    vec![TargetInfo {
                                        nodename: nodename.clone(),
                                        addresses: Some(vec![]),
                                        age_ms: Some(0),
                                        rcs_state: Some(RemoteControlState::Unknown),
                                        target_type: Some(TargetType::Unknown),
                                        target_state: Some(TargetState::Unknown),
                                        ..TargetInfo::EMPTY
                                    }]
                                } else {
                                    vec![
                                        TargetInfo {
                                            nodename: nodename.clone(),
                                            addresses: Some(vec![]),
                                            age_ms: Some(0),
                                            rcs_state: Some(RemoteControlState::Unknown),
                                            target_type: Some(TargetType::Unknown),
                                            target_state: Some(TargetState::Unknown),
                                            ..TargetInfo::EMPTY
                                        },
                                        TargetInfo {
                                            nodename: Some(UNRESPONSIVE_NODENAME.to_string()),
                                            addresses: Some(vec![]),
                                            age_ms: Some(0),
                                            rcs_state: Some(RemoteControlState::Unknown),
                                            target_type: Some(TargetType::Unknown),
                                            target_state: Some(TargetState::Unknown),
                                            ..TargetInfo::EMPTY
                                        },
                                    ]
                                }
                            },
                            move |query, target_handle| {
                                let waiter = waiter.clone();
                                spawn_target_handler(target_handle, move |req| match req {
                                    TargetRequest::OpenRemoteControl {
                                        responder,
                                        remote_control,
                                    } => {
                                        let target =
                                            query.string_matcher.as_deref().unwrap_or(NODENAME);
                                        if target == NODENAME {
                                            serve_responsive_rcs(remote_control);
                                        } else if target == UNRESPONSIVE_NODENAME {
                                            serve_unresponsive_rcs(remote_control, waiter.clone());
                                        } else {
                                            panic!("got unexpected target string: '{}'", target);
                                        }
                                        responder.send().unwrap();
                                    }
                                    r => panic!("unexpected request: {:?}", r),
                                });
                                Ok(())
                            },
                        );
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => {
                        assert!(false, "got unexpected request: {:?}", req);
                    }
                }
            }
        })
        .unwrap()
    }

    fn setup_daemon_server_list_fails() -> DaemonProxy {
        spawn_local_stream_handler(move |req| async move {
            match req {
                DaemonRequest::GetVersionInfo { responder } => {
                    responder.send(daemon_version_info()).unwrap();
                }
                DaemonRequest::ConnectToProtocol { name: _, server_end: _, responder } => {
                    // Do nothing with the server_end.
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => {
                    assert!(false, "got unexpected request: {:?}", req);
                }
            }
        })
        .unwrap()
    }

    fn setup_daemon_server_echo_hangs(waiter: Shared<Receiver<()>>) -> DaemonProxy {
        spawn_local_stream_handler(move |req| {
            let waiter = waiter.clone();
            async move {
                match req {
                    DaemonRequest::GetVersionInfo { responder: _ } => {
                        waiter.await.unwrap();
                    }
                    _ => {
                        assert!(false, "got unexpected request: {:?}", req);
                    }
                }
            }
        })
        .unwrap()
    }

    fn default_results_map() -> HashMap<TargetCheckResult, Vec<Option<String>>> {
        let mut map = HashMap::new();

        map.insert(TargetCheckResult::Success, vec![Some(NODENAME.to_string())]);
        map.insert(TargetCheckResult::Failed, vec![Some(UNRESPONSIVE_NODENAME.to_string())]);
        map
    }

    fn peer_closed() -> Error {
        fidl::Error::ClientChannelClosed {
            protocol_name: TargetCollectionMarker::NAME,
            status: fidl::handle::Status::PEER_CLOSED,
        }
        .into()
    }

    fn version_str() -> Option<String> {
        Some(String::from("fake version"))
    }

    fn daemon_version_info() -> VersionInfo {
        VersionInfo {
            commit_hash: None,
            commit_timestamp: None,
            build_version: Some(DAEMON_VERSION_STR.to_string()),
            ..VersionInfo::EMPTY
        }
    }

    fn record_params_no_record() -> DoctorRecorderParameters {
        DoctorRecorderParameters {
            record: false,
            user_config_enabled: false,
            log_root: None,
            output_dir: None,
            recorder: Arc::new(Mutex::new(DisabledRecorder::new())),
        }
    }

    fn record_params_with_temp(
        root: PathBuf,
    ) -> (Arc<Mutex<FakeRecorder>>, DoctorRecorderParameters) {
        let mut fe_log = root.clone();
        fe_log.push("ffx.log");
        let mut daemon_log = root.clone();
        daemon_log.push("ffx.daemon.log");
        let recorder =
            Arc::new(Mutex::new(FakeRecorder::new(vec![fe_log, daemon_log], root.clone())));
        (
            recorder.clone(),
            DoctorRecorderParameters {
                record: true,
                user_config_enabled: false,
                log_root: Some(root.clone()),
                output_dir: Some(root.clone()),
                recorder: recorder.clone(),
            },
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_no_daemon_running_no_targets_with_default_target() {
        let fake = FakeDaemonManager::new(
            vec![false],
            vec![Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_responsive_daemon_server())],
        );

        let mut handler = FakeStepHandler::new();
        doctor(
            &mut handler,
            &fake,
            "",
            1,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(Some(NODENAME.to_string())),
            record_params_no_record(),
        )
        .await
        .unwrap();

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(
                    Ok(Some(NODENAME.to_string())),
                    version_str(),
                )),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(NONE_RUNNING.to_string())),
                TestStepEntry::step(StepType::KillingZombieDaemons),
                TestStepEntry::result(StepResult::Other(NONE_RUNNING.to_string())),
                TestStepEntry::step(StepType::SpawningDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::NoTargetsFound),
                TestStepEntry::output_step(StepType::TerminalNoTargetsFound),
            ])
            .await;

        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_no_targets() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server())],
        );
        let mut handler = FakeStepHandler::new();

        doctor(
            &mut handler,
            &fake,
            "",
            1,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::NoTargetsFound),
                TestStepEntry::output_step(StepType::TerminalNoTargetsFound),
            ])
            .await;
        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_two_tries_daemon_running_list_fails() {
        let fake = FakeDaemonManager::new(
            vec![true, false],
            vec![Ok(true), Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_daemon_server_list_fails()), Ok(setup_daemon_server_list_fails())],
        );
        let mut handler = FakeStepHandler::new();

        doctor(
            &mut handler,
            &fake,
            "",
            2,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Error(peer_closed())),
                TestStepEntry::output_step(StepType::AttemptStarted(1, 2)),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(NONE_RUNNING.to_string())),
                TestStepEntry::step(StepType::KillingZombieDaemons),
                TestStepEntry::result(StepResult::Other(NONE_RUNNING.to_string())),
                TestStepEntry::step(StepType::SpawningDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Error(peer_closed())),
                TestStepEntry::output_step(StepType::TerminalNoTargetsFound),
            ])
            .await;

        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_two_tries_no_daemon_running_echo_timeout() {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![false, true],
            vec![Ok(false), Ok(true)],
            vec![Ok(())],
            vec![
                Ok(setup_daemon_server_echo_hangs(rx.shared())),
                Ok(setup_responsive_daemon_server()),
            ],
        );
        let mut handler = FakeStepHandler::new();
        doctor(
            &mut handler,
            &fake,
            "",
            2,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();
        tx.send(()).unwrap();
        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(NONE_RUNNING.to_string())),
                TestStepEntry::step(StepType::KillingZombieDaemons),
                TestStepEntry::result(StepResult::Other(NONE_RUNNING.to_string())),
                TestStepEntry::step(StepType::SpawningDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Timeout),
                TestStepEntry::output_step(StepType::AttemptStarted(1, 2)),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::NoTargetsFound),
                TestStepEntry::output_step(StepType::TerminalNoTargetsFound),
            ])
            .await;

        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_connects_to_rcs() {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets(true, rx.shared()))],
        );
        let mut handler = FakeStepHandler::new();
        doctor(
            &mut handler,
            &fake,
            "",
            1,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();
        tx.send(()).unwrap();

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::CheckingTarget(Some(NODENAME.to_string()))),
                TestStepEntry::step(StepType::OpeningTargetHandle(Some(NODENAME.to_string()))),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToRcs),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithRcs),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::CheckingTarget(Some(
                    UNRESPONSIVE_NODENAME.to_string(),
                ))),
                TestStepEntry::step(StepType::OpeningTargetHandle(Some(
                    UNRESPONSIVE_NODENAME.to_string(),
                ))),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToRcs),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithRcs),
                TestStepEntry::result(StepResult::Timeout),
                TestStepEntry::output_step(StepType::TargetSummary(default_results_map())),
                TestStepEntry::output_step(StepType::RcsTerminalFailure),
            ])
            .await;

        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_with_filter() {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets(true, rx.shared()))],
        );
        let mut handler = FakeStepHandler::new();
        doctor(
            &mut handler,
            &fake,
            &NODENAME,
            2,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();
        tx.send(()).unwrap();

        let mut map = HashMap::new();
        map.insert(TargetCheckResult::Success, vec![Some(NODENAME.to_string())]);

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(NODENAME.to_string())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::CheckingTarget(Some(NODENAME.to_string()))),
                TestStepEntry::step(StepType::OpeningTargetHandle(Some(NODENAME.to_string()))),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToRcs),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithRcs),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::TargetSummary(map)),
            ])
            .await;

        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_filter_finds_no_targets() {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets(true, rx.shared()))],
        );

        let mut handler = FakeStepHandler::new();
        doctor(
            &mut handler,
            &fake,
            &NON_EXISTENT_NODENAME,
            1,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();
        tx.send(()).unwrap();

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(NON_EXISTENT_NODENAME.to_string())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::NoTargetsFound),
                TestStepEntry::output_step(StepType::TerminalNoTargetsFound),
            ])
            .await;

        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_restart_daemon() {
        let fake = FakeDaemonManager::new(
            vec![false],
            vec![Ok(true), Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_responsive_daemon_server())],
        );
        let mut handler = FakeStepHandler::new();
        doctor(
            &mut handler,
            &fake,
            "",
            1,
            DEFAULT_RETRY_DELAY,
            true,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::output_step(StepType::DaemonForceRestart),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(NONE_RUNNING.to_string())),
                TestStepEntry::step(StepType::KillingZombieDaemons),
                TestStepEntry::result(StepResult::Other(NONE_RUNNING.to_string())),
                TestStepEntry::step(StepType::SpawningDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::NoTargetsFound),
                TestStepEntry::output_step(StepType::TerminalNoTargetsFound),
            ])
            .await;
        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_no_targets_record_enabled() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server())],
        );
        let mut handler = FakeStepHandler::new();
        let temp = tempdir().unwrap();
        let root = temp.path().to_path_buf();
        let (fake_recorder, params) = record_params_with_temp(root);

        doctor(
            &mut handler,
            &fake,
            "",
            1,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            params,
        )
        .await
        .unwrap();

        let r = fake_recorder.lock().await;
        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::NoTargetsFound),
                TestStepEntry::output_step(StepType::TerminalNoTargetsFound),
                TestStepEntry::step(StepType::GeneratingRecord),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::RecordGenerated(FakeRecorder::result_path())),
            ])
            .await;
        fake.assert_no_leftover_calls().await;
        r.assert_generate_called();
    }

    async fn missing_field_test(
        fake_recorder: Arc<Mutex<FakeRecorder>>,
        params: DoctorRecorderParameters,
    ) {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server())],
        );
        let mut handler = FakeStepHandler::new();

        assert!(doctor(
            &mut handler,
            &fake,
            "",
            1,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            params,
        )
        .await
        .is_err());

        let _ = fake_recorder.lock().await;
        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::NoTargetsFound),
                TestStepEntry::output_step(StepType::TerminalNoTargetsFound),
                // Error will occur here.
            ])
            .await;
        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_record_mode_missing_log_root_fails() {
        let temp = tempdir().unwrap();
        let root = temp.path().to_path_buf();
        let (fake_recorder, mut params) = record_params_with_temp(root);
        params.log_root = None;
        missing_field_test(fake_recorder, params).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_record_mode_missing_output_dir_fails() {
        let temp = tempdir().unwrap();
        let root = temp.path().to_path_buf();
        let (fake_recorder, mut params) = record_params_with_temp(root);
        params.output_dir = None;
        missing_field_test(fake_recorder, params).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_with_missing_nodename() {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets(false, rx.shared()))],
        );
        let mut handler = FakeStepHandler::new();
        doctor(
            &mut handler,
            &fake,
            "",
            1,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();
        tx.send(()).unwrap();

        let mut map = HashMap::new();
        map.insert(TargetCheckResult::Success, vec![None]);
        map.insert(TargetCheckResult::Failed, vec![Some(UNRESPONSIVE_NODENAME.to_string())]);

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::CheckingTarget(None)),
                TestStepEntry::step(StepType::OpeningTargetHandle(None)),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToRcs),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithRcs),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::CheckingTarget(Some(
                    UNRESPONSIVE_NODENAME.to_string(),
                ))),
                TestStepEntry::step(StepType::OpeningTargetHandle(Some(
                    UNRESPONSIVE_NODENAME.to_string(),
                ))),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ConnectingToRcs),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithRcs),
                TestStepEntry::result(StepResult::Timeout),
                TestStepEntry::output_step(StepType::TargetSummary(map)),
                TestStepEntry::output_step(StepType::RcsTerminalFailure),
            ])
            .await;

        fake.assert_no_leftover_calls().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_skips_fastboot_target() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_fastboot_target())],
        );
        let mut handler = FakeStepHandler::new();
        doctor(
            &mut handler,
            &fake,
            "",
            1,
            DEFAULT_RETRY_DELAY,
            false,
            version_str(),
            Ok(None),
            record_params_no_record(),
        )
        .await
        .unwrap();

        let mut map = HashMap::new();
        map.insert(TargetCheckResult::SkippedFastboot, vec![Some(FASTBOOT_NODENAME.to_string())]);

        handler
            .assert_matches_steps(vec![
                TestStepEntry::output_step(StepType::Started(Ok(None), version_str())),
                TestStepEntry::step(StepType::DaemonRunning),
                TestStepEntry::result(StepResult::Other(FOUND.to_string())),
                TestStepEntry::step(StepType::ConnectingToDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::CommunicatingWithDaemon),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::DaemonVersion(daemon_version_info())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::step(StepType::ListingTargets(String::default())),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::SkippedFastboot(Some(
                    FASTBOOT_NODENAME.to_string(),
                ))),
                TestStepEntry::output_step(StepType::TargetSummary(map)),
            ])
            .await;

        fake.assert_no_leftover_calls().await;
    }
}
