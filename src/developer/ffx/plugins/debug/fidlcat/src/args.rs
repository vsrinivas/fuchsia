// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Options for "ffx debug fidl".
#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "fidl",
    description = "uses fidlcat to automatically manage breakpoints to trace the fidl messages and/or the syscalls"
)]
pub struct FidlcatCommand {
    /// specifies the source.
    /// Source can be:
    ///
    /// device: this is the default input. The input comes from the live monitoring of one or
    /// several processes. At least one of '--remote-pid', '--remote-name', '--remote-job-id',
    /// --'remote-job-name', 'run' must be specified.
    ///
    /// dump: The input comes from stdin which is the log output of one or several programs. The
    /// lines in the log which dump syscalls are decoded and replaced by the decoded version.
    /// All other lines are unchanged.
    ///
    /// <path>: playback. Used to replay a session previously recorded with --to <path>
    /// (protobuf format). Path gives the name of the file to read. If path is '-' then the standard
    /// input is used.
    ///
    /// This option must be used at most once.
    #[argh(option)]
    pub from: Option<String>,

    /// the session is saved to the specified file (binary protobuf format).
    ///
    /// When a session is saved, you can replay it using "--from <path>".
    ///
    /// The raw data is saved. That means that the data saved is independent from what is displayed.
    #[argh(option)]
    pub to: Option<String>,

    /// the display format for the session dump. The available formats are:
    ///
    /// pretty: the session is pretty printed (with colors). This is the default output if --with is
    /// not used.
    ///
    /// json: the session is printed using a json format.
    ///
    /// textproto: the session is printed using a text protobuf format.
    ///
    /// none: nothing is displayed on the standard output (this option only makes sense when used
    /// with `--to` or with `--with`). When there is no output, fidlcat is faster (this is better to
    /// monitor real time components). This is the default output when --with is used.
    #[argh(option)]
    pub format: Option<String>,

    /// specifies an extra summarized output.
    ///
    /// summary: at the end of the session, a summary of the session is displayed on the standard
    /// output.
    ///
    /// top: at the end of the session, generate a view that groups the output by process, protocol,
    /// and method. The groups are sorted by number of events, so groups with more associated events
    /// are listed earlier.
    ///
    /// group-by-thread: for each thread display a short version of all the events.
    ///
    /// An equal sign followed by a path can be concatanated to the option to output the result in a
    /// file instead of the standard output (for example: --with summary=/tmp/x).
    ///
    /// This option can be used several times.
    #[argh(option)]
    pub with: Vec<String>,

    /// display the process name, process id and thread id on each line (useful for grep).
    #[argh(switch)]
    pub with_process_info: bool,

    /// define the amount of stack frame to display
    ///
    /// 0: none (default value)
    /// 1: call site (1 to 4 levels)
    /// 2: full stack frame (adds some overhead)
    #[argh(option)]
    pub stack: Option<String>,

    /// a regular expression which selects the syscalls to decode and display.
    ///
    /// This option can be specified multiple times.
    ///
    /// By default, only zx_channel_.* syscalls are displayed. To display all the syscalls, use:
    /// --syscalls ".*"
    #[argh(option)]
    pub syscalls: Vec<String>,

    /// a regular expression which selects the syscalls to not decode and display.
    ///
    /// This option can be specified multiple times.
    ///
    /// To be displayed, a syscall must verify --syscalls and not verify --exclude-syscalls.
    ///
    /// To display all the syscalls but the zx_handle syscalls, use:
    ///
    /// --syscalls ".*" --exclude-syscalls "zx_handle_.*"
    #[argh(option)]
    pub exclude_syscalls: Vec<String>,

    /// a regular expression which selects the messages to display.
    ///
    /// To display a message, the method name must satisfy the regexp.
    ///
    /// This option can be specified multiple times.
    ///
    /// Message filtering works on the method's fully qualified name.
    #[argh(option)]
    pub messages: Vec<String>,

    /// a regular expression which selects the messages to not display.
    ///
    /// If a message method name satisfy the regexp, the message is not displayed (even if it
    /// satisfies --messages).
    ///
    /// This option can be specified multiple times.
    ///
    /// Message filtering works on the method's fully qualified name.
    #[argh(option)]
    pub exclude_messages: Vec<String>,

    /// start displaying messages and syscalls only when a message for which the method name
    /// satisfies the filter is found.
    ///
    /// This option can be specified multiple times.
    ///
    /// Message filtering works on the method's fully qualified name.
    #[argh(option)]
    pub trigger: Vec<String>,

    /// only display the events for the specified thread.
    ///
    /// This option can be specified multiple times.
    ///
    /// By default all the events are displayed.
    #[argh(option)]
    pub thread: Vec<String>,

    /// always does a hexadecimal dump of the messages even if we can decode them.
    #[argh(switch)]
    pub dump_messages: bool,

    /// the koid of the remote process to trace.
    #[argh(option)]
    pub remote_pid: Vec<String>,

    /// the <name> of a process.
    /// Fidlcat will monitor all existing and future processes whose names includes <name>
    /// (<name> is a substring of the process name).
    ///
    /// This option can be specified multiple times.
    ///
    /// When used with --remote-job-id or --remote-job-name, only the processes from the selected
    /// jobs are taken into account.
    #[argh(option)]
    pub remote_name: Vec<String>,

    /// like --remote-name, it monitors some processes. However, for these processes, monitoring
    /// starts only when one of of the "--remote-name" process is launched.
    ///
    /// Also, fidlcat stops when the last "--remote-name" process stops (even if some "--extra-name"
    ///  processes are still monitored).
    ///
    /// This option can be specified multiple times.
    #[argh(option)]
    pub extra_name: Vec<String>,

    /// the koid of a remote job for which we want to monitor all the processes.
    ///
    /// Only jobs created before fidlcat is launched are monitored.
    ///
    /// This option can be specified multiple times.
    #[argh(option)]
    pub remote_job_id: Vec<String>,

    /// the name of a remote job for which we want to monitor all the processes. All the jobs which
    /// contain <name> in their name are used.
    ///
    /// Only jobs created before fidlcat is launched are monitored.
    ///
    /// This option can be specified multiple times.
    #[argh(option)]
    pub remote_job_name: Vec<String>,
}
