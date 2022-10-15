#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Upload reproxy logs and metrics to BQ tables.

This is used to publish fine-grained remote build performance data.
"""

import argparse
import glob
import json
import os
import subprocess
import sys
import tempfile
import uuid

import pb_message_util
from api.proxy import log_pb2
from api.stats import stats_pb2
import rbe_metrics_pb2
from typing import Any, Callable, Dict, Sequence, Tuple

_SCRIPT_BASENAME = os.path.basename(__file__)
# This script lives at _PROJECT_ROOT/build/rbe/{__file__}.
_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))

# There is never a need to checkout non-host platforms of the reclient tools.
# This should be unique.  Path may be relative or absolute.
_DEFAULT_RECLIENT_BINDIR = glob.glob(
    os.path.join(_PROJECT_ROOT,
                 "prebuilt/proprietary/third_party/reclient/*"))[0]

_DEFAULT_REPROXY_LOGS_TABLE = "fuchsia-engprod-metrics-prod:metrics.rbe_client_command_logs_developer"
_DEFAULT_RBE_METRICS_TABLE = "fuchsia-engprod-metrics-prod:metrics.rbe_client_metrics_developer"


def msg(text: str):
    print(f"[{_SCRIPT_BASENAME}] {text}")


def table_arg(value: str) -> str:
    err_msg = "Table name must be in the form PROJECT:DATASET.TABLE"
    project, sep, dataset_table = value.partition(':')
    if not sep:
        raise argparse.ArgumentTypeError(err_msg)
    dataset, sep, table = dataset_table.partition('.')
    if not sep:
        raise argparse.ArgumentTypeError(err_msg)
    if not (project and dataset and table):
        raise argparse.ArgumentTypeError(err_msg)
    return value


def dir_arg(value: str) -> str:
    if not os.path.isdir(value):
        raise argparse.ArgumentTypeError("Argument must be a directory.")
    return value


def main_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Upload reproxy logs and metrics.",
        argument_default=[],
    )
    parser.add_argument(
        "--reclient-bindir",
        type=dir_arg,
        help="Location of reclient binaries",
        default=_DEFAULT_RECLIENT_BINDIR,
    )
    parser.add_argument(
        "--uuid",
        type=str,
        help="Unique ID string for this build",
    )
    parser.add_argument(
        "--upload-batch-size",
        type=int,
        default=1000,
        help="Number of remote action log entries to upload at a time",
    )
    parser.add_argument(
        "--bq-logs-table",
        type=table_arg,
        default=_DEFAULT_REPROXY_LOGS_TABLE,
        help=
        "BigQuery remote action logs table name in the form 'project:dataset.table'",
    )
    parser.add_argument(
        "--bq-metrics-table",
        type=table_arg,
        default=_DEFAULT_RBE_METRICS_TABLE,
        help=
        "BigQuery remote action metrics table name in the form 'project:dataset.table'",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        default=False,
        help="Ingest log and metrics data, but do not perform upload.",
    )
    parser.add_argument(
        "--print-sample",
        action="store_true",
        default=False,
        help="Print one remote action log entry.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        default=False,
        help="Show upload steps and progress.",
    )
    parser.add_argument(
        "--auth-only",
        action="store_true",
        default=False,
        help=
        "Authenticate by inserting a null entry to a table, and do nothing else.",
    )

    # Positional args are the reproxy logdirs to process.
    parser.add_argument(
        "reproxy_logdirs",
        nargs="*",
        help="The reproxy log dirs to upload",
    )

    return parser


def convert_reproxy_actions_log(
        reproxy_logdir: str, reclient_bindir: str) -> log_pb2.LogDump:
    # Convert reproxy text logs to binary proto.
    logdump_cmd = [
        os.path.join(reclient_bindir, "logdump"),
        "--proxy_log_dir",
        reproxy_logdir,
        "--output_dir",
        reproxy_logdir,  # Use same log dir, must be writeable.
    ]
    subprocess.check_call(logdump_cmd)
    # Produces "reproxy_log.pb" in args.reproxy_logdir.

    log_dump = log_pb2.LogDump()
    with open(os.path.join(reproxy_logdir, "reproxy_log.pb"),
              mode='rb') as logf:
        log_dump.ParseFromString(logf.read())

    return log_dump


def read_reproxy_metrics_proto(metrics_file: str) -> stats_pb2.Stats:
    stats = stats_pb2.Stats()
    with open(metrics_file, mode='rb') as f:
        stats.ParseFromString(f.read())
    return stats


def bq_table_insert(table: str, data: str) -> int:
    # The 'bq' CLI tool comes with gcloud SDK.
    # Unfortunately, piping the data through stdin doesn't work
    # because bq expects an interactive session, so we use a temp file.
    with tempfile.NamedTemporaryFile() as f:
        f.write(data.encode())
        f.flush()
        return subprocess.call(['bq', 'insert', table, f.name])


def bq_upload_remote_action_logs(
    records: Sequence[Dict[str, Any]],
    bq_table: str,
    batch_size: int,
) -> int:
    batches = (
        records[i:i + batch_size] for i in range(0, len(records), batch_size))
    exit_code = 0
    for batch in batches:
        # bq accepts rows as newline-delimited JSON.
        data = "\n".join(json.dumps(row) for row in batch)
        _exit_code = bq_table_insert(bq_table, data)
        if _exit_code != 0:
            # There will be something printed to stderr already.
            exit_code = _exit_code

    return exit_code


def bq_upload_metrics(
    metrics: Sequence[Dict[str, Any]],
    bq_table: str,
) -> int:
    data = "\n".join(json.dumps(row) for row in metrics)
    return bq_table_insert(bq_table, data)


def main_upload_metrics(
    uuid: str,
    reproxy_logdir: str,
    bq_metrics_table: str,
    dry_run: bool = False,
    verbose: bool = False,
) -> int:
    if verbose:
        msg(f"Ingesting reproxy metrics from {reproxy_logdir}")

    metrics_file = os.path.join(reproxy_logdir, "rbe_metrics.pb")
    stats = read_reproxy_metrics_proto(metrics_file=metrics_file)

    if len(stats.stats) == 0:
        if verbose:
            msg("No remote action stats found.  Skipping upload.")
        return 0

    metrics_pb = rbe_metrics_pb2.RbeMetrics(
        build_id=uuid,
        stats=stats,
    )
    metrics_pb.created_at.GetCurrentTime()

    if verbose:
        msg(f"Converting metrics format to JSON for BQ.")
    metrics_json = pb_message_util.proto_message_to_bq_dict(metrics_pb)

    if dry_run:
        return 0

    if verbose:
        msg("Uploading aggregate metrics BQ")
    exit_code = bq_upload_metrics(
        metrics=[metrics_json],
        bq_table=bq_metrics_table,
    )
    if exit_code != 0:
        msg("There was at least one error uploading metrics.")
    elif verbose:
        msg("Done uploading RBE metrics.")

    return exit_code


def main_upload_logs(
    uuid: str,
    reproxy_logdir: str,
    reclient_bindir: str,
    bq_logs_table: str,
    upload_batch_size: int,
    dry_run: bool = False,
    verbose: bool = False,
    print_sample: bool = False,
) -> int:
    if verbose:
        msg(f"Ingesting reproxy action logs from {reproxy_logdir}")
    log_dump = convert_reproxy_actions_log(
        reproxy_logdir=reproxy_logdir,
        reclient_bindir=reclient_bindir,
    )

    if len(log_dump.records) == 0:
        if verbose:
            msg("No remote action records found.  Skipping upload.")
        return 0

    if verbose:
        msg(f"Converting log format to JSON for BQ.")
    converted_log = pb_message_util.proto_message_to_bq_dict(log_dump)

    # Attach build id to log entries
    log_records = [
        {
            "build_id": uuid,
            "log": record,
        } for record in converted_log["records"]
    ]

    if print_sample:
        msg("Sample remote action record:")
        print(log_records[0])
        return 0

    if dry_run:
        return 0

    if verbose:
        msg("Uploading converted logs to BQ")

    exit_code = bq_upload_remote_action_logs(
        records=log_records,
        bq_table=bq_logs_table,
        batch_size=upload_batch_size,
    )
    if exit_code != 0:
        msg("There was at least one error uploading action logs.")
    elif verbose:
        msg("Done uploading RBE logs.")

    return exit_code


def main_single_logdir(
    reproxy_logdir: str,
    reclient_bindir: str,
    metrics_table: str,
    logs_table: str,
    uuid_flag: str,
    upload_batch_size: str,
    print_sample: bool,
    dry_run: bool,
    verbose: bool,
) -> int:

    # The rbe_metrics.pb file is a sign that a build finished.
    # Skip over unfinished builds.
    metrics_file = os.path.join(reproxy_logdir, "rbe_metrics.pb")
    if not os.path.isfile(metrics_file):
        if verbose:
            msg(
                f"Metrics file {metrics_file} not found.  Assuming build is not finished and skipping {reproxy_logdir}."
            )
        return 0

    build_id_file = os.path.join(reproxy_logdir, "build_id")

    # Use a stamp-file to know whether or not this directory has been uploaded.
    upload_stamp_file = os.path.join(reproxy_logdir, "upload_stamp")
    if not dry_run and os.path.exists(upload_stamp_file):
        # An uploaded log dir already has a build_id.
        with open(build_id_file) as f:
            build_id = f.read().strip(" \n")
        msg(
            f"Already uploaded {reproxy_logdir} with build_id {build_id}.  Skipping."
        )
        return 0

    # Make sure we have a uuid.
    # "build_id" comes from build/rbe/fuchsia-reproxy-wrap.sh.
    if uuid_flag:
        build_id = uuid_flag
    elif os.path.isfile(build_id_file):
        with open(build_id_file) as f:
            build_id = f.read().strip(" \n")
    else:
        # Some log dirs were created before we started adding build ids.
        # If needed, create one, and write it to the same file.
        build_id = str(uuid.uuid4())
        with open(build_id_file, 'w') as f:
            f.write(build_id + "\n")

    # Upload aggregate metrics.
    exit_code = main_upload_metrics(
        uuid=build_id,
        reproxy_logdir=reproxy_logdir,
        bq_metrics_table=metrics_table,
        dry_run=dry_run,
        verbose=verbose,
    )
    if exit_code != 0:
        return exit_code

    # Upload remote action logs.
    exit_code = main_upload_logs(
        uuid=build_id,
        reproxy_logdir=reproxy_logdir,
        reclient_bindir=reclient_bindir,  # for logdump utility
        bq_logs_table=logs_table,
        upload_batch_size=upload_batch_size,
        dry_run=dry_run,
        verbose=verbose,
        print_sample=print_sample,
    )
    if exit_code != 0:
        return exit_code

    # Leave a stamp-file to indicate we've already uploaded this reproxy_logdir.
    if not dry_run:
        with open(upload_stamp_file, 'w') as f:
            f.write(
                "Already uploaded {reproxy_logdir}.  Remove {upload_stamp_file} and re-run to force re-upload."
            )

    return 0


def main(argv: Sequence[str]) -> int:
    parser = main_arg_parser()
    args = parser.parse_args(argv)

    if args.auth_only:
        # Ignore any args.reproxy_logdirs.
        # A `bq insert` should trigger authentication and get a refresh token.
        if args.verbose:
            msg("Authenticating BQ upload access for metrics and logs.")
        return bq_table_insert(args.bq_logs_table, "")

    exit_code = 0
    for logdir in args.reproxy_logdirs:
        _exit_code = main_single_logdir(
            reproxy_logdir=logdir,
            reclient_bindir=args.reclient_bindir,
            metrics_table=args.bq_metrics_table,
            logs_table=args.bq_logs_table,
            uuid_flag=args.uuid,
            upload_batch_size=args.upload_batch_size,
            print_sample=args.print_sample,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
        if _exit_code != 0:
            exit_code = _exit_code

    return exit_code


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
