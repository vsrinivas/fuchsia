// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::*,
    crate::doctor_ledger::*,
    crate::ledger_view::*,
    anyhow::{anyhow, Context, Result},
    async_lock::Mutex,
    async_trait::async_trait,
    doctor_utils::{DaemonManager, DefaultDaemonManager, DoctorRecorder, Recorder},
    errors::ffx_bail,
    ffx_config::{get, print_config},
    ffx_core::ffx_plugin,
    ffx_doctor_new_args::DoctorCommand,
    fidl::endpoints::create_proxy,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_bridge::{
        TargetCollectionMarker, TargetCollectionProxy, TargetCollectionReaderMarker,
        TargetCollectionReaderRequest, TargetInfo, TargetMarker, TargetQuery, TargetState,
        VersionInfo,
    },
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    futures::TryStreamExt,
    serde_json::json,
    std::sync::Arc,
    std::{
        collections::HashSet,
        io::{stdout, BufWriter, Write},
        path::PathBuf,
        process::Command,
        time::Duration,
    },
    termion::{color, style},
    timeout::timeout,
};

mod constants;
mod doctor_ledger;
mod ledger_view;

const DEFAULT_TARGET_CONFIG: &str = "target.default";
const DOCTOR_OUTPUT_FILENAME: &str = "doctor_output.txt";
const PLATFORM_INFO_FILENAME: &str = "platform.json";
const USER_CONFIG_FILENAME: &str = "user_config.txt";
const RECORD_CONFIG_SETTING: &str = "doctor.record_config";

#[derive(Debug, PartialEq)]
enum StepType {
    DoctorSummaryInitNormal(),
    DoctorSummaryInitVerbose(),
    GeneratingRecord,
    Output(String),
    RecordGenerated(PathBuf),
}

#[derive(Debug)]
enum StepResult {
    Success,
}

impl std::fmt::Display for StepResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = match self {
            StepResult::Success => SUCCESS.to_string(),
        };

        write!(f, "{}", s)
    }
}

impl std::fmt::Display for StepType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = match self {
            StepType::DoctorSummaryInitNormal() => {
                let msg = "Doctor summary (to see all details, run ffx doctor-new -v):".to_string();
                format!("\n{}{}{}\n", style::Bold, msg, style::Reset)
            }
            StepType::DoctorSummaryInitVerbose() => {
                let msg = "Doctor summary:".to_string();
                format!("\n{}{}{}\n", style::Bold, msg, style::Reset)
            }
            StepType::GeneratingRecord => "Generating record...".to_string(),
            StepType::Output(data_str) => {
                format!("{}", data_str)
            }
            StepType::RecordGenerated(path) => {
                format!("Record generated at: {}\n", path.to_string_lossy().into_owned())
            }
        };

        write!(f, "{}", s)
    }
}

#[async_trait]
trait DoctorStepHandler {
    async fn step(&mut self, step: StepType) -> Result<()>;
    async fn output_step(&mut self, step: StepType) -> Result<()>;
    async fn record(&mut self, step: StepType) -> Result<()>;
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

    async fn record(&mut self, step: StepType) -> Result<()> {
        let mut r = self.recorder.lock().await;
        r.add_content(DOCTOR_OUTPUT_FILENAME, format!("{}", step));
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

struct DoctorRecorderParameters {
    record: bool,
    user_config_enabled: bool,
    log_root: Option<PathBuf>,
    output_dir: Option<PathBuf>,
    recorder: Arc<Mutex<dyn Recorder>>,
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

    // create ledger
    let ledger_mode = match cmd.verbose {
        true => LedgerViewMode::Verbose,
        false => LedgerViewMode::Normal,
    };
    let mut ledger = DoctorLedger::<std::io::Stdout>::new(
        stdout(),
        Box::new(VisualLedgerView::new()),
        ledger_mode,
    );

    doctor(
        &mut handler,
        &mut ledger,
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

async fn doctor<W: Write>(
    step_handler: &mut impl DoctorStepHandler,
    ledger: &mut DoctorLedger<W>,
    daemon_manager: &impl DaemonManager,
    target_str: &str,
    _retry_count: usize,
    retry_delay: Duration,
    restart_daemon: bool,
    build_version_string: Option<String>,
    default_target: Result<Option<String>, String>,
    record_params: DoctorRecorderParameters,
) -> Result<()> {
    if restart_daemon {
        doctor_daemon_restart(daemon_manager, retry_delay, ledger).await?;
    } else {
        doctor_summary(
            step_handler,
            daemon_manager,
            target_str,
            retry_delay,
            build_version_string,
            default_target,
            ledger,
        )
        .await?;
    }

    if record_params.record {
        let mut record_view = RecordLedgerView::new();
        let data = ledger.write_all(&mut record_view)?;
        step_handler.record(StepType::Output(data)).await?;
        doctor_record(step_handler, record_params).await?;
    }

    Ok(())
}

async fn doctor_record(
    step_handler: &mut impl DoctorStepHandler,
    record_params: DoctorRecorderParameters,
) -> Result<()> {
    let log_root =
        record_params.log_root.context("log_root not present despite record set to true")?;
    let output_dir =
        record_params.output_dir.context("output_dir not present despite record set to true")?;

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
                    Some(zip::result::ZipError::Io(io_error)) => match io_error.raw_os_error() {
                        Some(27) => Err(anyhow!(
                            "{}\nMake sure you can write files larger than 1MB to: {}\n{}",
                            e,
                            &path,
                            advice
                        ))?,
                        _ => default_err_msg?,
                    },
                    _ => default_err_msg?,
                }
            }
        }
    };

    step_handler.result(StepResult::Success).await?;
    step_handler.output_step(StepType::RecordGenerated(final_path.canonicalize()?)).await?;
    Ok(())
}

async fn get_daemon_pid<W: Write>(
    daemon_manager: &impl DaemonManager,
    ledger: &mut DoctorLedger<W>,
) -> Option<Vec<usize>> {
    match daemon_manager.get_pid().await {
        Ok(vec) => return Some(vec),
        Err(e) => {
            let node = ledger
                .add_node(&format!("Error getting daemon pid: {}", e), LedgerMode::Automatic)
                .ok()?;
            ledger.set_outcome(node, LedgerOutcome::SoftWarning).ok()?;
            return None;
        }
    }
}

// Return the elements of `a` that are not in `b`.
// Note: this function preserves order for simpler testing.
fn difference(a: &Vec<usize>, b: &Vec<usize>) -> Vec<usize> {
    let sb: HashSet<usize> = b.iter().cloned().collect();
    a.iter().filter(|&e| !sb.contains(e)).cloned().collect()
}

// Update the current, the added, and the dropped daemon pids.
// Display if there are any errors while fetching the pids.
// Note: we display pid fetching error only one time. has_error is set once there is an error.
async fn calc_daemon_pid_diff<W: Write>(
    has_error: &mut bool,
    daemon_manager: &impl DaemonManager,
    ledger: &mut DoctorLedger<W>,
    current_pids: &mut Vec<usize>,
    added_pids: &mut Vec<usize>,
    dropped_pids: &mut Vec<usize>,
) {
    // Setup
    added_pids.clear();
    dropped_pids.clear();

    if *has_error {
        current_pids.clear();
        return ();
    }

    // Get pid vector
    let new_pids = match get_daemon_pid(daemon_manager, ledger).await {
        Some(v) => v,
        None => {
            current_pids.clear();
            *has_error = true;
            return ();
        }
    };

    // Update
    added_pids.extend(difference(&new_pids, current_pids));
    dropped_pids.extend(difference(current_pids, &new_pids));
    current_pids.clear();
    current_pids.extend(new_pids);
}

fn format_vec(a: &Vec<usize>) -> String {
    format!(
        "[{}]",
        a.iter()
            .enumerate()
            .map(|(i, v)| match i {
                0 => format!("{}", v),
                _ => format!(", {}", v),
            })
            .collect::<String>(),
    )
}

async fn daemon_restart<W: Write>(
    daemon_manager: &impl DaemonManager,
    retry_delay: Duration,
    ledger: &mut DoctorLedger<W>,
) -> Result<()> {
    let mut main_node = ledger.add_node("Killing Daemon", LedgerMode::Automatic)?;

    let mut error_pid = false;
    let mut cur_pids = Vec::<usize>::new();
    let mut add_pids = Vec::<usize>::new();
    let mut sub_pids = Vec::<usize>::new();
    let error = &mut error_pid;
    let cpid = &mut cur_pids;
    let apid = &mut add_pids;
    let spid = &mut sub_pids;

    calc_daemon_pid_diff(error, daemon_manager, ledger, cpid, apid, spid).await;

    // Kill the daemon if it is running.
    let daemon_killed = if daemon_manager.is_running().await {
        let node = ledger.add_node("Killing running daemons.", LedgerMode::Automatic)?;
        daemon_manager.kill_all().await?;
        ledger.set_outcome(node, LedgerOutcome::Success)?;
        true
    } else {
        if daemon_manager.kill_all().await? {
            let node = ledger.add_node("Killing zombie daemons.", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
            true
        } else {
            let node = ledger.add_node("No running daemons found.", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
            false
        }
    };

    // Display killed daemon PIDs.
    calc_daemon_pid_diff(error, daemon_manager, ledger, cpid, apid, spid).await;
    if daemon_killed && !*error {
        {
            let node = ledger.add_node(
                &format!("Killed daemon PID: {}", format_vec(spid)),
                LedgerMode::Automatic,
            )?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
        }

        if cpid.len() > 0 {
            let node = ledger.add_node(
                &format!("Daemon are still running, PID: {}", format_vec(cpid)),
                LedgerMode::Automatic,
            )?;
            ledger.set_outcome(node, LedgerOutcome::Warning)?;
        }
    }

    ledger.close(main_node)?;

    if daemon_killed {
        // HACK: Wait a few seconds before spawning a new daemon. Attempting
        // to spawn one too quickly after killing one will lead to timeouts
        // when attempting to communicate with the spawned daemon.
        // Temporary fix for fxbug.dev/66958. Remove when that bug is resolved.
        fuchsia_async::Timer::new(Duration::from_millis(5000)).await;
    };

    main_node = ledger.add_node("Starting Daemon", LedgerMode::Automatic)?;

    // Spawn daemon.
    match timeout(retry_delay, daemon_manager.spawn()).await {
        Ok(Ok(_)) => {
            let node = ledger.add_node("Daemon spawned", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
        }
        Ok(Err(e)) => {
            let node =
                ledger.add_node(&format!("Error spawning daemon: {}", e), LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
        Err(_) => {
            let node = ledger.add_node("Timeout spawning daemon", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
    }

    calc_daemon_pid_diff(error, daemon_manager, ledger, cpid, apid, spid).await;
    if !*error {
        let node =
            ledger.add_node(&format!("Daemon PID: {}", format_vec(apid)), LedgerMode::Automatic)?;
        ledger.set_outcome(node, LedgerOutcome::Success)?;
    }

    // Check daemon connection.
    let daemon_proxy = match timeout(retry_delay, daemon_manager.find_and_connect()).await {
        Ok(Ok(val)) => {
            let node = ledger.add_node("Connected to daemon", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
            val
        }
        Ok(Err(e)) => {
            let node = ledger
                .add_node(&format!("Error connecting to daemon: {}", e), LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
        Err(_) => {
            let node =
                ledger.add_node("Timeout while connecting to daemon", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
    };

    match timeout(retry_delay, daemon_proxy.get_version_info()).await {
        Ok(Ok(v)) => {
            let daemon_version = v.build_version.clone().unwrap_or("Unknown".to_string());
            let node = ledger
                .add_node(&format!("Daemon version: {}", daemon_version), LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
        }
        Ok(Err(e)) => {
            let node = ledger
                .add_node(&format!("Error getting daemon version: {}", e), LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
        Err(_) => {
            let node =
                ledger.add_node("Timeout while getting daemon version", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
    }

    ledger.close(main_node)?;
    return Ok(());
}

async fn doctor_daemon_restart<W: Write>(
    daemon_manager: &impl DaemonManager,
    spawn_delay: Duration,
    ledger: &mut DoctorLedger<W>,
) -> Result<()> {
    match daemon_restart(daemon_manager, spawn_delay, ledger).await {
        Err(err) => {
            let node = ledger.add_node(&format!("Error: {}", err), LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
        }
        _ => (),
    };
    return Ok(());
}

async fn doctor_summary<W: Write>(
    step_handler: &mut impl DoctorStepHandler,
    daemon_manager: &impl DaemonManager,
    target_str: &str,
    retry_delay: Duration,
    build_version_string: Option<String>,
    default_target: Result<Option<String>, String>,
    ledger: &mut DoctorLedger<W>,
) -> Result<()> {
    match ledger.get_ledger_mode() {
        LedgerViewMode::Normal => {
            step_handler.output_step(StepType::DoctorSummaryInitNormal()).await?
        }
        LedgerViewMode::Verbose => {
            step_handler.output_step(StepType::DoctorSummaryInitVerbose()).await?
        }
    }

    let mut main_node = ledger.add_node("FFX doctor", LedgerMode::Automatic)?;

    let version_node = ledger.add_node(
        &format!("Frontend version: {}", build_version_string.unwrap_or("UNKNOWN".to_string())),
        LedgerMode::Verbose,
    )?;
    ledger.set_outcome(version_node, LedgerOutcome::Success)?;
    ledger.close(main_node)?;

    main_node = ledger.add_node("Checking daemon", LedgerMode::Automatic)?;

    if daemon_manager.is_running().await {
        let pid_vec = get_daemon_pid(daemon_manager, ledger).await.unwrap_or_default();
        let node = ledger
            .add_node(&format!("Daemon found: {}", format_vec(&pid_vec)), LedgerMode::Automatic)?;
        ledger.set_outcome(node, LedgerOutcome::Success)?;
    } else {
        let node = ledger.add_node(
            "No running daemons found. Run `ffx doctor --restart-daemon`",
            LedgerMode::Automatic,
        )?;
        ledger.set_outcome(node, LedgerOutcome::Failure)?;
        ledger.close(main_node)?;
        return Ok(());
    }

    let daemon_proxy = match timeout(retry_delay, daemon_manager.find_and_connect()).await {
        Ok(Ok(val)) => {
            let node = ledger.add_node("Connecting to daemon", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
            val
        }
        Ok(Err(e)) => {
            let node = ledger
                .add_node(&format!("Error connecting to daemon: {}", e), LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
        Err(_) => {
            let node =
                ledger.add_node("Timeout while connecting to daemon", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
    };

    match timeout(retry_delay, daemon_proxy.get_version_info()).await {
        Ok(Ok(v)) => {
            let daemon_version = v.build_version.clone().unwrap_or("Unknown".to_string());
            let node = ledger
                .add_node(&format!("Daemon version: {}", daemon_version), LedgerMode::Verbose)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
        }
        Ok(Err(e)) => {
            let node = ledger
                .add_node(&format!("Error getting daemon version: {}", e), LedgerMode::Verbose)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            // Continue, not a critical error.
        }
        Err(_) => {
            let node =
                ledger.add_node("Timeout while getting daemon version", LedgerMode::Verbose)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            // Continue, not a critical error.
        }
    }

    match default_target {
        Ok(Some(target_name)) => {
            let node = ledger
                .add_node(&format!("Default target: {}", target_name), LedgerMode::Verbose)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
        }
        Ok(_) => {
            let node = ledger.add_node("Default target: (none)", LedgerMode::Verbose)?;
            ledger.set_outcome(node, LedgerOutcome::Success)?;
        }
        Err(e) => {
            let node =
                ledger.add_node(&format!("config read failed: {:?}", e), LedgerMode::Verbose)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
        }
    }

    ledger.close(main_node)?;
    main_node = ledger.add_node("Searching for targets", LedgerMode::Automatic)?;

    let (tc_proxy, tc_server) = fidl::endpoints::create_proxy::<TargetCollectionMarker>()?;
    match timeout(
        retry_delay,
        daemon_proxy.connect_to_protocol(TargetCollectionMarker::NAME, tc_server.into_channel()),
    )
    .await
    {
        Ok(Err(e)) => {
            let node = ledger.add_node(
                &format!("Error connecting to target service: {}", e),
                LedgerMode::Verbose,
            )?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
        Ok(_) => {}
        Err(_) => {
            let node = ledger
                .add_node("Timeout while connecting to target service", LedgerMode::Verbose)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
    }

    let targets = match timeout(retry_delay, list_targets(Some(target_str), &tc_proxy)).await {
        Ok(Ok(list)) => {
            if list.len() > 0 {
                let node = ledger
                    .add_node(&format!("{} targets found", list.len()), LedgerMode::Automatic)?;
                ledger.set_outcome(node, LedgerOutcome::Success)?;
                list
            } else {
                let node = ledger.add_node("No targets found!", LedgerMode::Automatic)?;
                ledger.set_outcome(node, LedgerOutcome::Failure)?;
                ledger.close(main_node)?;
                return Ok(());
            }
        }
        Ok(Err(e)) => {
            let node =
                ledger.add_node(&format!("Error getting targets: {}", e), LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
        Err(_) => {
            let node =
                ledger.add_node("Timeout while getting target list", LedgerMode::Automatic)?;
            ledger.set_outcome(node, LedgerOutcome::Failure)?;
            ledger.close(main_node)?;
            return Ok(());
        }
    };

    ledger.close(main_node)?;
    let mut verify_inode = LedgerNode::new("Verifying Targets".to_string(), LedgerMode::Normal);
    verify_inode.set_fold_function(OutcomeFoldFunction::FailureToSuccess, LedgerOutcome::Failure);
    main_node = ledger.add(verify_inode)?;

    for target in targets.iter() {
        let target_name = match target.nodename.clone() {
            Some(v) => v,
            None => {
                let node =
                    ledger.add_node("Skipping target without node name", LedgerMode::Automatic)?;
                ledger.set_outcome(node, LedgerOutcome::SoftWarning)?;
                continue;
            }
        };

        // Note: this match statement intentionally does not have a fallback case in order to
        // ensure that behavior is considered when we add a new state.
        match target.target_state {
            None => {}
            Some(TargetState::Unknown) => {}
            Some(TargetState::Disconnected) => {}
            Some(TargetState::Product) => {}
            Some(TargetState::Fastboot) => {
                let node = ledger.add_node(
                    &format!("Skipping target in fastboot: {}", target_name),
                    LedgerMode::Automatic,
                )?;
                ledger.set_outcome(node, LedgerOutcome::SoftWarning)?;
                continue;
            }
            Some(TargetState::Zedboot) => {
                let node = ledger.add_node(
                    &format!("Skipping target in zedboot: {}", target_name),
                    LedgerMode::Automatic,
                )?;
                ledger.set_outcome(node, LedgerOutcome::SoftWarning)?;
                continue;
            }
        }
        let target_node =
            ledger.add_node(&format!("Target: {}", target_name), LedgerMode::Normal)?;

        //TODO(fxbug.dev/86523): Offer a fix when we cannot connect to a device via RCS.
        let (target_proxy, target_server) = fidl::endpoints::create_proxy::<TargetMarker>()?;
        match timeout(
            retry_delay,
            tc_proxy.open_target(
                TargetQuery { string_matcher: target.nodename.clone(), ..TargetQuery::EMPTY },
                target_server,
            ),
        )
        .await
        {
            Ok(Ok(_)) => {
                let node = ledger.add_node("Opened target handle", LedgerMode::Verbose)?;
                ledger.set_outcome(node, LedgerOutcome::Success)?;
            }
            Ok(Err(e)) => {
                let node = ledger.add_node(
                    &format!("Error while opening target handle: {}", e),
                    LedgerMode::Verbose,
                )?;
                ledger.set_outcome(node, LedgerOutcome::Failure)?;
                ledger.close(target_node)?;
                continue;
            }
            Err(_) => {
                let node =
                    ledger.add_node("Timeout while opening target handle", LedgerMode::Verbose)?;
                ledger.set_outcome(node, LedgerOutcome::Failure)?;
                ledger.close(target_node)?;
                continue;
            }
        }

        let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;

        match timeout(retry_delay, target_proxy.open_remote_control(remote_server_end)).await {
            Ok(Ok(_)) => {
                let node = ledger.add_node("Connecting to RCS", LedgerMode::Verbose)?;
                ledger.set_outcome(node, LedgerOutcome::Success)?;
            }
            Ok(Err(e)) => {
                let node = ledger.add_node(
                    &format!("Error while connecting to RCS: {}", e),
                    LedgerMode::Verbose,
                )?;
                ledger.set_outcome(node, LedgerOutcome::Failure)?;
                ledger.close(target_node)?;
                continue;
            }
            Err(_) => {
                let node =
                    ledger.add_node("Timeout while connecting to RCS", LedgerMode::Verbose)?;
                ledger.set_outcome(node, LedgerOutcome::Failure)?;
                ledger.close(target_node)?;
                continue;
            }
        }

        match timeout(retry_delay, remote_proxy.identify_host()).await {
            Ok(Ok(_)) => {
                let node = ledger.add(LedgerNode::new(
                    "Communicating with RCS".to_string(),
                    LedgerMode::Verbose,
                ))?;
                ledger.set_outcome(node, LedgerOutcome::Success)?;
            }
            Ok(Err(e)) => {
                let node = ledger.add_node(
                    &format!("Error while communicating with RCS: {}", e),
                    LedgerMode::Verbose,
                )?;
                ledger.set_outcome(node, LedgerOutcome::Failure)?;
                ledger.close(target_node)?;
                continue;
            }
            Err(_) => {
                let node =
                    ledger.add_node("Timeout while communicating with RCS", LedgerMode::Verbose)?;
                ledger.set_outcome(node, LedgerOutcome::Failure)?;
                ledger.close(target_node)?;
                continue;
            }
        }

        ledger.close(target_node)?;
    }

    ledger.close(main_node)?;

    match ledger.calc_outcome(main_node) {
        LedgerOutcome::Failure => {
            let msg = match ledger.get_ledger_mode() {
                LedgerViewMode::Normal => String::from(
                    "Doctor found issues in one or more categories; \
                    run 'ffx doctor -v' for more details.",
                ),
                _ => String::from("Doctor found issues in one or more categories."),
            };
            main_node = ledger.add_node(&msg, LedgerMode::Automatic)?;
            ledger.set_outcome(main_node, LedgerOutcome::Failure)?;
        }
        _ => {
            main_node = ledger.add_node("No issues found", LedgerMode::Automatic)?;
            ledger.set_outcome(main_node, LedgerOutcome::Success)?;
        }
    }

    Ok(())
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Tests
///////////////////////////////////////////////////////////////////////////////////////////////////

#[cfg(test)]
mod test {
    use {
        super::*,
        async_lock::Mutex,
        async_trait::async_trait,
        ffx_doctor_test_utils::MockWriter,
        fidl::endpoints::RequestStream,
        fidl::endpoints::{spawn_local_stream_handler, ProtocolMarker, Request, ServerEnd},
        fidl::Channel,
        fidl_fuchsia_developer_bridge::{
            DaemonProxy, DaemonRequest, OpenTargetError, RemoteControlState,
            TargetCollectionRequest, TargetCollectionRequestStream, TargetInfo, TargetRequest,
            TargetState, TargetType,
        },
        fidl_fuchsia_developer_remotecontrol::{
            IdentifyHostResponse, RemoteControlMarker, RemoteControlRequest,
        },
        fuchsia_async as fasync,
        futures::channel::oneshot::{self, Receiver},
        futures::future::Shared,
        futures::{Future, FutureExt, TryFutureExt, TryStreamExt},
        std::cell::Cell,
        std::collections::HashSet,
        std::fmt,
        std::sync::Arc,
        tempfile::tempdir,
    };

    const NODENAME: &str = "fake-nodename";
    const UNRESPONSIVE_NODENAME: &str = "fake-nodename-unresponsive";
    const FASTBOOT_NODENAME: &str = "fastboot-nodename-unresponsive";
    const NON_EXISTENT_NODENAME: &str = "extra-fake-nodename";
    const DEFAULT_RETRY_DELAY: Duration = Duration::from_millis(2000);
    const DAEMON_VERSION: &str = "daemon-build-string";
    const FRONTEND_VERSION: &str = "fake version";
    const INDENT_STR: &str = "    ";

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
                    (StepResult::Success, StepResult::Success) => true,
                },
                (None, None) => true,
                _ => false,
            }
        }
    }

    struct FakeLedgerView {
        tree: LedgerViewNode,
    }

    impl FakeLedgerView {
        pub fn new() -> Self {
            FakeLedgerView { tree: LedgerViewNode::default() }
        }
        fn gen_output(&self, parent_node: &LedgerViewNode, indent_level: usize) -> String {
            let mut data = parent_node.data.clone();
            // Remove error details to make the tests more stable
            if data.starts_with("Error") {
                let v: Vec<_> = data.split(":").collect();
                if v.len() > 1 {
                    data = format!("{}: <reason omitted>", v.first().unwrap().to_string());
                }
            }

            let mut output_str = format!(
                "{}[{}] {}\n",
                INDENT_STR.repeat(indent_level),
                parent_node.outcome.format(false),
                data
            );

            for child_node in &parent_node.children {
                let child_str = self.gen_output(child_node, indent_level + 1);
                output_str = format!("{}{}", output_str, child_str);
            }

            return output_str;
        }
    }

    impl fmt::Display for FakeLedgerView {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "{}", self.gen_output(&self.tree, 0))
        }
    }

    impl LedgerView for FakeLedgerView {
        fn set(&mut self, new_tree: LedgerViewNode) {
            self.tree = new_tree;
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

        async fn record(&mut self, step: StepType) -> Result<()> {
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
        get_pid_results: Vec<Result<Vec<usize>>>,
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
            get_pid_results: Vec<Result<Vec<usize>>>,
        ) -> Self {
            return FakeDaemonManager {
                state_manager: Arc::new(Mutex::new(FakeStateManager {
                    kill_results,
                    daemons_running_results,
                    spawn_results,
                    find_and_connect_results,
                    get_pid_results,
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

        async fn get_pid(&self) -> Result<Vec<usize>> {
            let mut state = self.state_manager.lock().await;
            assert!(!state.get_pid_results.is_empty(), "too many calls to spawn");
            state.get_pid_results.remove(0)
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

    // Spawns a target collection, accepting closures for handling listing and opening target
    // handles.
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

    fn version_str() -> Option<String> {
        Some(FRONTEND_VERSION.to_string())
    }

    fn daemon_version_info() -> VersionInfo {
        VersionInfo {
            commit_hash: None,
            commit_timestamp: None,
            build_version: Some(DAEMON_VERSION.to_string()),
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
            vec![],
        );

        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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

        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
                   \n[✓] FFX doctor\
                   \n    [✓] Frontend version: {}\
                   \n[✗] Checking daemon\
                   \n    [✗] No running daemons found. Run `ffx doctor --restart-daemon`\n",
                FRONTEND_VERSION
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_no_targets() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server())],
            vec![Ok(vec![1])],
        );
        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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

        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
                   \n[✓] FFX doctor\
                   \n    [✓] Frontend version: {}\
                   \n[✓] Checking daemon\
                   \n    [✓] Daemon found: [1]\
                   \n    [✓] Connecting to daemon\
                   \n    [✓] Daemon version: {}\
                   \n    [✓] Default target: (none)\
                   \n[✗] Searching for targets\
                   \n    [✗] No targets found!\n",
                FRONTEND_VERSION, DAEMON_VERSION
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_two_tries_daemon_running_list_fails() {
        let fake = FakeDaemonManager::new(
            vec![true, false],
            vec![Ok(true), Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_daemon_server_list_fails()), Ok(setup_daemon_server_list_fails())],
            vec![Ok(vec![1])],
        );
        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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

        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] FFX doctor\
            \n    [✓] Frontend version: {}\
            \n[✓] Checking daemon\
            \n    [✓] Daemon found: [1]\
            \n    [✓] Connecting to daemon\
            \n    [✓] Daemon version: {}\
            \n    [✓] Default target: (none)\
            \n[✗] Searching for targets\
            \n    [✗] Error getting targets: <reason omitted>\n",
                FRONTEND_VERSION, DAEMON_VERSION
            )
        );
    }

    #[fasync::run_singlethreaded(test)]

    async fn test_two_tries_no_daemon_running_echo_timeout() {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![false, true],
            vec![Ok(false), Ok(true)],
            vec![Ok(()), Ok(())],
            vec![
                Ok(setup_daemon_server_echo_hangs(rx.shared())),
                Ok(setup_responsive_daemon_server()),
            ],
            vec![Ok(vec![]), Ok(vec![]), Ok(vec![1]), Ok(vec![2]), Ok(vec![]), Ok(vec![3])],
        );
        let mut handler = FakeStepHandler::new();

        // restart daemon
        {
            let ledger_view = Box::new(FakeLedgerView::new());
            let mut ledger = DoctorLedger::<MockWriter>::new(
                MockWriter::new(),
                ledger_view,
                LedgerViewMode::Verbose,
            );

            doctor(
                &mut handler,
                &mut ledger,
                &fake,
                "",
                2,
                DEFAULT_RETRY_DELAY,
                true,
                version_str(),
                Ok(None),
                record_params_no_record(),
            )
            .await
            .unwrap();

            assert_eq!(
                ledger.writer.get_data(),
                "\
                    \n[✓] Killing Daemon\
                    \n    [✓] No running daemons found.\
                    \n[✗] Starting Daemon\
                    \n    [✓] Daemon spawned\
                    \n    [✓] Daemon PID: [1]\
                    \n    [✓] Connected to daemon\
                    \n    [✗] Timeout while getting daemon version\
                    \n"
            );
        }

        // restart daemon
        {
            let ledger_view = Box::new(FakeLedgerView::new());
            let mut ledger = DoctorLedger::<MockWriter>::new(
                MockWriter::new(),
                ledger_view,
                LedgerViewMode::Verbose,
            );

            doctor(
                &mut handler,
                &mut ledger,
                &fake,
                "",
                2,
                DEFAULT_RETRY_DELAY,
                true,
                version_str(),
                Ok(None),
                record_params_no_record(),
            )
            .await
            .unwrap();

            assert_eq!(
                ledger.writer.get_data(),
                format!(
                    "\
                    \n[✓] Killing Daemon\
                    \n    [✓] Killing running daemons.\
                    \n    [✓] Killed daemon PID: [2]\
                    \n[✓] Starting Daemon\
                    \n    [✓] Daemon spawned\
                    \n    [✓] Daemon PID: [3]\
                    \n    [✓] Connected to daemon\
                    \n    [✓] Daemon version: {}\
                    \n",
                    DAEMON_VERSION
                )
            );
        }

        tx.send(()).unwrap();
    }

    async fn test_finds_target_connects_to_rcs_setup(
        mode: LedgerViewMode,
    ) -> DoctorLedger<MockWriter> {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets(true, rx.shared()))],
            vec![Ok(vec![1])],
        );
        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(MockWriter::new(), ledger_view, mode);

        doctor(
            &mut handler,
            &mut ledger,
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

        return ledger;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_connects_to_rcs_verbose() {
        let ledger = test_finds_target_connects_to_rcs_setup(LedgerViewMode::Verbose).await;
        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] FFX doctor\
            \n    [✓] Frontend version: {}\
            \n[✓] Checking daemon\
            \n    [✓] Daemon found: [1]\
            \n    [✓] Connecting to daemon\
            \n    [✓] Daemon version: {}\
            \n    [✓] Default target: (none)\
            \n[✓] Searching for targets\
            \n    [✓] 2 targets found\
            \n[✓] Verifying Targets\
            \n    [✓] Target: {}\
            \n        [✓] Opened target handle\
            \n        [✓] Connecting to RCS\
            \n        [✓] Communicating with RCS\
            \n    [✗] Target: {}\
            \n        [✓] Opened target handle\
            \n        [✓] Connecting to RCS\
            \n        [✗] Timeout while communicating with RCS\
            \n[✓] No issues found\n",
                FRONTEND_VERSION, DAEMON_VERSION, NODENAME, UNRESPONSIVE_NODENAME,
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_connects_to_rcs_normal() {
        let ledger = test_finds_target_connects_to_rcs_setup(LedgerViewMode::Normal).await;
        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] Checking daemon\
            \n    [✓] Daemon found: [1]\
            \n    [✓] Connecting to daemon\
            \n[✓] Searching for targets\
            \n    [✓] 2 targets found\
            \n[✓] Verifying Targets\
            \n    [✓] Target: {}\
            \n    [✗] Target: {}\
            \n[✓] No issues found\n",
                NODENAME, UNRESPONSIVE_NODENAME,
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_with_filter() {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets(true, rx.shared()))],
            vec![Ok(vec![1])],
        );
        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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

        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] FFX doctor\
            \n    [✓] Frontend version: {}\
            \n[✓] Checking daemon\
            \n    [✓] Daemon found: [1]\
            \n    [✓] Connecting to daemon\
            \n    [✓] Daemon version: {}\
            \n    [✓] Default target: (none)\
            \n[✓] Searching for targets\
            \n    [✓] 1 targets found\
            \n[✓] Verifying Targets\
            \n    [✓] Target: {}\
            \n        [✓] Opened target handle\
            \n        [✓] Connecting to RCS\
            \n        [✓] Communicating with RCS\
            \n[✓] No issues found\n",
                FRONTEND_VERSION, DAEMON_VERSION, NODENAME,
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_filter_finds_no_targets() {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets(true, rx.shared()))],
            vec![Ok(vec![1])],
        );

        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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

        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] FFX doctor\
            \n    [✓] Frontend version: {}\
            \n[✓] Checking daemon\
            \n    [✓] Daemon found: [1]\
            \n    [✓] Connecting to daemon\
            \n    [✓] Daemon version: {}\
            \n    [✓] Default target: (none)\
            \n[✗] Searching for targets\
            \n    [✗] No targets found!\n",
                FRONTEND_VERSION, DAEMON_VERSION
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_restart_daemon() {
        let fake = FakeDaemonManager::new(
            vec![false],
            vec![Ok(true), Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_responsive_daemon_server())],
            vec![Ok(vec![1, 2, 3]), Ok(vec![]), Ok(vec![4])],
        );
        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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

        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] Killing Daemon\
            \n    [✓] Killing zombie daemons.\
            \n    [✓] Killed daemon PID: [1, 2, 3]\
            \n[✓] Starting Daemon\
            \n    [✓] Daemon spawned\
            \n    [✓] Daemon PID: [4]\
            \n    [✓] Connected to daemon\
            \n    [✓] Daemon version: {}\
            \n",
                DAEMON_VERSION
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_restart_daemon_pid_error() {
        let fake = FakeDaemonManager::new(
            vec![false],
            vec![Ok(true), Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_responsive_daemon_server())],
            vec![
                Err(anyhow!("some error msg")),
                Err(anyhow!("some error msg")),
                Err(anyhow!("some error msg")),
            ],
        );
        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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

        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] Killing Daemon\
            \n    [!] Error getting daemon pid: <reason omitted>\
            \n    [✓] Killing zombie daemons.\
            \n[✓] Starting Daemon\
            \n    [✓] Daemon spawned\
            \n    [✓] Connected to daemon\
            \n    [✓] Daemon version: {}\
            \n",
                DAEMON_VERSION
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_no_targets_record_enabled() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server())],
            vec![Ok(vec![1])],
        );
        let mut handler = FakeStepHandler::new();
        let temp = tempdir().unwrap();
        let root = temp.path().to_path_buf();
        let (fake_recorder, params) = record_params_with_temp(root);

        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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
                TestStepEntry::output_step(StepType::DoctorSummaryInitVerbose()),
                TestStepEntry::output_step(StepType::Output(format!(
                    "\
                [✓] FFX doctor\
                \n    [✓] Frontend version: {}\n\n\
                [✓] Checking daemon\
                \n    [✓] Daemon found: [1]\
                \n    [✓] Connecting to daemon\
                \n    [✓] Daemon version: daemon-build-string\
                \n    [✓] Default target: (none)\n\n\
                [✗] Searching for targets\
                \n    [✗] No targets found!\n\n",
                    FRONTEND_VERSION
                ))),
                TestStepEntry::step(StepType::GeneratingRecord),
                TestStepEntry::result(StepResult::Success),
                TestStepEntry::output_step(StepType::RecordGenerated(FakeRecorder::result_path())),
            ])
            .await;
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
            vec![Ok(vec![1])],
        );
        let mut handler = FakeStepHandler::new();

        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        assert!(doctor(
            &mut handler,
            &mut ledger,
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
                TestStepEntry::output_step(StepType::DoctorSummaryInitVerbose()),
                TestStepEntry::output_step(StepType::Output(format!(
                    "\
                [✓] FFX doctor\
                \n    [✓] Frontend version: {}\n\n\
                [✓] Checking daemon\
                \n    [✓] Daemon found: [1]\
                \n    [✓] Connecting to daemon\
                \n    [✓] Daemon version: daemon-build-string\
                \n    [✓] Default target: (none)\n\n\
                [✗] Searching for targets\
                \n    [✗] No targets found!\n\n",
                    FRONTEND_VERSION
                ))),
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

    async fn test_finds_target_with_missing_nodename_setup(
        mode: LedgerViewMode,
    ) -> DoctorLedger<MockWriter> {
        let (tx, rx) = oneshot::channel::<()>();

        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets(false, rx.shared()))],
            vec![Ok(vec![1])],
        );

        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(MockWriter::new(), ledger_view, mode);

        doctor(
            &mut handler,
            &mut ledger,
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

        return ledger;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_with_missing_nodename_verbose() {
        let ledger = test_finds_target_with_missing_nodename_setup(LedgerViewMode::Verbose).await;
        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] FFX doctor\
            \n    [✓] Frontend version: {}\
            \n[✓] Checking daemon\
            \n    [✓] Daemon found: [1]\
            \n    [✓] Connecting to daemon\
            \n    [✓] Daemon version: {}\
            \n    [✓] Default target: (none)\
            \n[✓] Searching for targets\
            \n    [✓] 2 targets found\
            \n[✗] Verifying Targets\
            \n    [!] Skipping target without node name\
            \n    [✗] Target: {}\
            \n        [✓] Opened target handle\
            \n        [✓] Connecting to RCS\
            \n        [✗] Timeout while communicating with RCS\
            \n[✗] Doctor found issues in one or more categories.\n",
                FRONTEND_VERSION, DAEMON_VERSION, UNRESPONSIVE_NODENAME,
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_with_missing_nodename_normal() {
        let ledger = test_finds_target_with_missing_nodename_setup(LedgerViewMode::Normal).await;
        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] Checking daemon\
            \n    [✓] Daemon found: [1]\
            \n    [✓] Connecting to daemon\
            \n[✓] Searching for targets\
            \n    [✓] 2 targets found\
            \n[✗] Verifying Targets\
            \n    [!] Skipping target without node name\
            \n    [✗] Target: {}\
            \n[✗] Doctor found issues in one or more categories; \
            run 'ffx doctor -v' for more details.\n",
                UNRESPONSIVE_NODENAME,
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_skips_fastboot_target() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_fastboot_target())],
            vec![Ok(vec![1])],
        );
        let mut handler = FakeStepHandler::new();
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = DoctorLedger::<MockWriter>::new(
            MockWriter::new(),
            ledger_view,
            LedgerViewMode::Verbose,
        );

        doctor(
            &mut handler,
            &mut ledger,
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

        assert_eq!(
            ledger.writer.get_data(),
            format!(
                "\
            \n[✓] FFX doctor\
            \n    [✓] Frontend version: {}\
            \n[✓] Checking daemon\
            \n    [✓] Daemon found: [1]\
            \n    [✓] Connecting to daemon\
            \n    [✓] Daemon version: {}\
            \n    [✓] Default target: (none)\
            \n[✓] Searching for targets\
            \n    [✓] 1 targets found\
            \n[✗] Verifying Targets\
            \n    [!] Skipping target in fastboot: {}\
            \n[✗] Doctor found issues in one or more categories.\n",
                FRONTEND_VERSION, DAEMON_VERSION, FASTBOOT_NODENAME
            )
        );
    }
}
