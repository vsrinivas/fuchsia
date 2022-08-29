#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Upload reproxy logs to a table.

This is used to publish fine-grain anonymized remote build performance data.
"""

import argparse
import os
import subprocess
import sys
import pb_message_util
from api.proxy import log_pb2
from typing import Any, Callable, Dict, Sequence, Tuple

_SCRIPT_BASENAME = os.path.basename(__file__)


def msg(text: str):
    print(f"[{_SCRIPT_BASENAME}] {text}")


def table_arg(value: str) -> str:
    parts = value.split('.')
    err_msg = "Table name must be in the form PROJECT.DATASET.TABLE"
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(err_msg)
    project, dataset, table = parts
    if not (project and dataset and table):
        raise argparse.ArgumentTypeError(err_msg)
    return value


def dir_arg(value: str) -> str:
    if not os.path.isdir(value):
        raise argparse.ArgumentTypeError("Argument must be a directory.")
    return value


def main_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Upload reproxy logs.",
        argument_default=[],
    )
    parser.add_argument(
        "--reclient-bindir",
        type=dir_arg,
        help="Location of reclient binaries",
        required=True,
    )
    parser.add_argument(
        "--reproxy-logdir",
        type=dir_arg,
        help="Location of reproxy logs",
        required=True,
    )
    parser.add_argument(
        "--uuid",
        type=str,
        help="Unique ID string for this build",
    )
    parser.add_argument(
        "--bqupload",
        # Should be executable or symlink to executable.
        type=argparse.FileType(mode='r'),
        help="Path to 'bqupload' tool",
        required=True,
    )
    parser.add_argument(
        "--upload-batch-size",
        type=int,
        default=1000,
        help="Number of log entries to upload at a time",
    )
    parser.add_argument(
        "--bq-table",
        type=table_arg,
        help="BigQuery table name in the form 'project.dataset.table'",
        required=True,
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        default=False,
        help="Ingest log data, but do not perform upload.",
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
    return parser


def convert_reproxy_log(
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


def upload_records(
    records: Sequence[Dict[str, Any]],
    bqupload: str,
    bq_table: str,
    batch_size: int,
):
    batches = (
        records[i:i + batch_size] for i in range(0, len(records), batch_size))
    any_err = False
    for batch in batches:
        # bqupload accepts rows as newline-delimited JSON.
        data = "\n".join(str(row) for row in batch)
        exit_code = subprocess.call([bqupload, bq_table, data])
        if exit_code != 0:
            # There will be something printed to stderr already.
            any_err = True

    if any_err:
        print("There was at least one error uploading logs with bqupload.")


def main(argv: Sequence[str]) -> int:
    parser = main_arg_parser()
    args = parser.parse_args(argv)

    # Use a stamp-file to know whether or not this directory has been uploaded.
    upload_stamp_file = os.path.join(args.reproxy_logdir, "upload_stamp")
    if os.path.exists(upload_stamp_file):
        msg(f"Already uploaded logs in {args.reproxy_log_dir}.  Skipping.")
        return 0

    # Make sure we have a uuid.
    # "build_id" comes from build/rbe/fuchsia-reproxy-wrap.sh.
    build_id_file = os.path.join(args.reproxy_logdir, "build_id")
    if args.uuid:
        uuid = args.uuid
    elif os.path.isfile(build_id_file):
        with open(build_id_file) as f:
            uuid = f.read().strip(" \n")
    else:
        msg(f"Need a build id from either --uuid or {build_id_file}")
        return 1

    if args.verbose:
        msg(f"Ingesting reproxy logs from {args.reproxy_logdir}")
    log_dump = convert_reproxy_log(
        reproxy_logdir=args.reproxy_logdir,
        reclient_bindir=args.reclient_bindir,
    )

    if args.verbose:
        msg(f"Anonymizing records.")
    for record in log_dump.records:
        record.command.exec_root = "/home/anonymous/user"

    if args.verbose:
        msg(f"Converting log format to JSON for BQ.")
    converted_log = pb_message_util.proto_message_to_bq_dict(log_dump)

    # Attach build id to log entries
    records = [
        {
            "build_id": uuid,
            "log": record,
        } for record in converted_log["records"]
    ]

    if args.print_sample:
        print("Sample record:")
        print(records[0])
        return 0

    if not args.dry_run:
        if args.verbose:
            msg("Uploading converted logs to BQ")
        upload_records(
            records=records,
            bqupload=args.bqupload,
            bq_table=args.bq_table,
            batch_size=args.upload_batch_size,
        )
        with open(upload_stamp_file, 'w') as f:
            f.write(
                "Already uploaded.  Remove this file and re-run to re-upload.")

        msg("Done uploading RBE logs.")

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
