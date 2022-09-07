#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import unittest
from unittest import mock

from api.proxy import log_pb2
from api.stats import stats_pb2

import upload_reproxy_logs

# Most tests here are testing for python syntax/semantic errors.


class MainUploadMetricsTest(unittest.TestCase):

    def test_dry_run(self):
        with mock.patch.object(
                upload_reproxy_logs, "read_reproxy_metrics_proto",
                return_value=stats_pb2.Stats()) as mock_read_proto:
            upload_reproxy_logs.main_upload_metrics(
                uuid="feed-face-feed-face",
                reproxy_logdir="/tmp/reproxy.log.dir",
                bqupload="/usr/local/bin/bqupload",
                bq_metrics_table="project.dataset.rbe_metrics",
                dry_run=True,
                verbose=False)
        mock_read_proto.assert_called_once()

    def test_mocked_upload(self):
        with mock.patch.object(
                upload_reproxy_logs, "read_reproxy_metrics_proto",
                return_value=stats_pb2.Stats(
                    stats=[stats_pb2.Stat()])) as mock_read_proto:
            with mock.patch.object(upload_reproxy_logs,
                                   "bq_upload_metrics") as mock_upload:
                upload_reproxy_logs.main_upload_metrics(
                    uuid="feed-face-feed-face",
                    reproxy_logdir="/tmp/reproxy.log.dir",
                    bqupload="/usr/local/bin/bqupload",
                    bq_metrics_table="project.dataset.rbe_metrics",
                    dry_run=False,
                    verbose=False)
        mock_read_proto.assert_called_once()
        mock_upload.assert_called_once()

    def test_empty_stats(self):
        with mock.patch.object(
                upload_reproxy_logs, "read_reproxy_metrics_proto",
                return_value=stats_pb2.Stats()) as mock_read_proto:
            with mock.patch.object(upload_reproxy_logs,
                                   "bq_upload_metrics") as mock_upload:
                upload_reproxy_logs.main_upload_metrics(
                    uuid="feed-face-feed-face",
                    reproxy_logdir="/tmp/reproxy.log.dir",
                    bqupload="/usr/local/bin/bqupload",
                    bq_metrics_table="project.dataset.rbe_metrics",
                    dry_run=False,
                    verbose=False)
        mock_read_proto.assert_called_once()
        mock_upload.assert_not_called()


class MainUploadLogsTest(unittest.TestCase):

    def fake_log(self):
        return log_pb2.LogDump(records=[log_pb2.LogRecord()])

    def test_dry_run(self):
        with mock.patch.object(
                upload_reproxy_logs, "convert_reproxy_actions_log",
                return_value=self.fake_log()) as mock_convert_log:
            upload_reproxy_logs.main_upload_logs(
                uuid="feed-f00d-feed-f00d",
                reproxy_logdir="/tmp/reproxy.log.dir",
                reclient_bindir="/usr/local/reclient/bin",
                bqupload="/usr/local/bin/bqupload",
                bq_logs_table="project.dataset.reproxy_logs",
                upload_batch_size=100,
                dry_run=True,
                verbose=False,
                print_sample=False,
            )
        mock_convert_log.assert_called_once()

    def test_mocked_upload(self):
        with mock.patch.object(
                upload_reproxy_logs, "convert_reproxy_actions_log",
                return_value=self.fake_log()) as mock_convert_log:
            with mock.patch.object(
                    upload_reproxy_logs,
                    "bq_upload_remote_action_logs") as mock_upload:
                upload_reproxy_logs.main_upload_logs(
                    uuid="feed-f00d-feed-f00d",
                    reproxy_logdir="/tmp/reproxy.log.dir",
                    reclient_bindir="/usr/local/reclient/bin",
                    bqupload="/usr/local/bin/bqupload",
                    bq_logs_table="project.dataset.reproxy_logs",
                    upload_batch_size=100,
                    dry_run=False,
                    verbose=False,
                    print_sample=False,
                )
        mock_convert_log.assert_called_once()
        mock_upload.assert_called_once()

    def test_empty_records(self):
        with mock.patch.object(
                upload_reproxy_logs, "convert_reproxy_actions_log",
                return_value=log_pb2.LogDump()) as mock_convert_log:
            with mock.patch.object(
                    upload_reproxy_logs,
                    "bq_upload_remote_action_logs") as mock_upload:
                upload_reproxy_logs.main_upload_logs(
                    uuid="feed-f00d-feed-f00d",
                    reproxy_logdir="/tmp/reproxy.log.dir",
                    reclient_bindir="/usr/local/reclient/bin",
                    bqupload="/usr/local/bin/bqupload",
                    bq_logs_table="project.dataset.reproxy_logs",
                    upload_batch_size=100,
                    dry_run=False,
                    verbose=False,
                    print_sample=False,
                )
        mock_convert_log.assert_called_once()
        mock_upload.assert_not_called()


class ConvertReproxyActionsLogTest(unittest.TestCase):

    def test_basic(self):

        with mock.patch.object(subprocess, "check_call") as mock_process_call:
            with mock.patch.object(__builtins__, "open") as mock_open:
                with mock.patch.object(log_pb2.LogDump,
                                       "ParseFromString") as mock_parse:
                    log_dump = upload_reproxy_logs.convert_reproxy_actions_log(
                        reproxy_logdir="/tmp/reproxy.log.dir",
                        reclient_bindir="/usr/local/reclient/bin",
                    )
        mock_process_call.assert_called_once()
        mock_open.assert_called_once()
        mock_parse.assert_called_once()
        self.assertEqual(log_dump, log_pb2.LogDump())


class ReadReproxyMetricsProto(unittest.TestCase):

    def test_basic(self):
        with mock.patch.object(__builtins__, "open") as mock_open:
            with mock.patch.object(stats_pb2.Stats,
                                   "ParseFromString") as mock_parse:
                stats = upload_reproxy_logs.read_reproxy_metrics_proto(
                    reproxy_logdir="/tmp/reproxy.log.dir",
                )
        mock_open.assert_called_once()
        mock_parse.assert_called_once()
        self.assertEqual(stats, stats_pb2.Stats())


class BQUploadRemoteActionLogsTest(unittest.TestCase):

    def test_batch_upload(self):
        bqupload = "/path/to/bqupload"
        bq_table = "proj.dataset.tablename"
        with mock.patch.object(subprocess, "call",
                               side_effect=[0, 0]) as mock_process_call:
            upload_reproxy_logs.bq_upload_remote_action_logs(
                records=[{
                    "records": []
                }] * 8,
                bqupload=bqupload,
                bq_table=bq_table,
                batch_size=4,
            )
        mock_process_call.assert_called_with(
            [bqupload, bq_table, "\n".join(["{'records': []}"] * 4)])


class BQUploadMetricsTest(unittest.TestCase):

    def test_upload(self):
        bqupload = "/path/to/bqupload"
        bq_table = "proj.dataset.tablename"
        with mock.patch.object(subprocess, "call",
                               return_value=0) as mock_process_call:
            upload_reproxy_logs.bq_upload_metrics(
                metrics=[{
                    "metrics": []
                }],
                bqupload=bqupload,
                bq_table=bq_table,
            )
        mock_process_call.assert_called_with(
            [bqupload, bq_table, "{'metrics': []}"])


if __name__ == '__main__':
    unittest.main()
