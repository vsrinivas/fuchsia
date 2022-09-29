#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from api.proxy import log_pb2
from api.stats import stats_pb2

import upload_reproxy_logs

# Most tests here are testing for python syntax/semantic errors.


class ReproxyLogdirTestHarness(unittest.TestCase):

    def setUp(self):
        self._reproxy_logdir = tempfile.mkdtemp()
        # The majority of tests expect the metrics file to be present
        # as a sign that a build is done.
        self.touch_metrics_file()

    def tearDown(self):
        shutil.rmtree(self._reproxy_logdir)

    @property
    def _stamp_file(self):
        return os.path.join(self._reproxy_logdir, "upload_stamp")

    @property
    def _build_id_file(self):
        return os.path.join(self._reproxy_logdir, "build_id")

    @property
    def _metrics_file(self):
        return os.path.join(self._reproxy_logdir, "rbe_metrics.pb")

    def touch_stamp_file(self):
        with open(self._stamp_file, "w") as f:
            f.write("\n")

    def touch_metrics_file(self):
        with open(self._metrics_file, "wb") as f:
            f.write("\n".encode())


class MainUploadMetricsTest(ReproxyLogdirTestHarness):

    def test_dry_run(self):
        with mock.patch.object(
                upload_reproxy_logs, "read_reproxy_metrics_proto",
                return_value=stats_pb2.Stats()) as mock_read_proto:
            exit_code = upload_reproxy_logs.main_upload_metrics(
                uuid="feed-face-feed-face",
                reproxy_logdir=self._reproxy_logdir,
                bq_metrics_table="project.dataset.rbe_metrics",
                dry_run=True,
                verbose=False)
        mock_read_proto.assert_called_once()
        self.assertEqual(exit_code, 0)

    def test_mocked_upload(self):
        with mock.patch.object(
                upload_reproxy_logs, "read_reproxy_metrics_proto",
                return_value=stats_pb2.Stats(
                    stats=[stats_pb2.Stat()])) as mock_read_proto:
            with mock.patch.object(upload_reproxy_logs, "bq_upload_metrics",
                                   return_value=0) as mock_upload:
                exit_code = upload_reproxy_logs.main_upload_metrics(
                    uuid="feed-face-feed-face",
                    reproxy_logdir=self._reproxy_logdir,
                    bq_metrics_table="project.dataset.rbe_metrics",
                    dry_run=False,
                    verbose=False)
        mock_read_proto.assert_called_once()
        mock_upload.assert_called_once()
        self.assertEqual(exit_code, 0)

    def test_mocked_upload_failure(self):
        with mock.patch.object(
                upload_reproxy_logs, "read_reproxy_metrics_proto",
                return_value=stats_pb2.Stats(
                    stats=[stats_pb2.Stat()])) as mock_read_proto:
            with mock.patch.object(upload_reproxy_logs, "bq_upload_metrics",
                                   return_value=1) as mock_upload:
                exit_code = upload_reproxy_logs.main_upload_metrics(
                    uuid="feed-face-feed-face",
                    reproxy_logdir=self._reproxy_logdir,
                    bq_metrics_table="project.dataset.rbe_metrics",
                    dry_run=False,
                    verbose=False)
        mock_read_proto.assert_called_once()
        mock_upload.assert_called_once()
        self.assertEqual(exit_code, 1)

    def test_empty_stats(self):
        with open(self._metrics_file, "wb") as metrics_file:
            pass
        with mock.patch.object(
                upload_reproxy_logs, "read_reproxy_metrics_proto",
                return_value=stats_pb2.Stats()) as mock_read_proto:
            with mock.patch.object(upload_reproxy_logs, "bq_upload_metrics",
                                   return_value=0) as mock_upload:
                exit_code = upload_reproxy_logs.main_upload_metrics(
                    uuid="feed-face-feed-face",
                    reproxy_logdir=self._reproxy_logdir,
                    bq_metrics_table="project.dataset.rbe_metrics",
                    dry_run=False,
                    verbose=False)
        mock_read_proto.assert_called_once()
        mock_upload.assert_not_called()
        self.assertEqual(exit_code, 0)


class MainUploadLogsTest(unittest.TestCase):

    def fake_log(self):
        return log_pb2.LogDump(records=[log_pb2.LogRecord()])

    def test_dry_run(self):
        with mock.patch.object(
                upload_reproxy_logs, "convert_reproxy_actions_log",
                return_value=self.fake_log()) as mock_convert_log:
            exit_code = upload_reproxy_logs.main_upload_logs(
                uuid="feed-f00d-feed-f00d",
                reproxy_logdir="/tmp/reproxy.log.dir",
                reclient_bindir="/usr/local/reclient/bin",
                bq_logs_table="project.dataset.reproxy_logs",
                upload_batch_size=100,
                dry_run=True,
                verbose=False,
                print_sample=False,
            )
        mock_convert_log.assert_called_once()
        self.assertEqual(exit_code, 0)

    def test_mocked_upload(self):
        with mock.patch.object(
                upload_reproxy_logs, "convert_reproxy_actions_log",
                return_value=self.fake_log()) as mock_convert_log:
            with mock.patch.object(upload_reproxy_logs,
                                   "bq_upload_remote_action_logs",
                                   return_value=0) as mock_upload:
                exit_code = upload_reproxy_logs.main_upload_logs(
                    uuid="feed-f00d-feed-f00d",
                    reproxy_logdir="/tmp/reproxy.log.dir",
                    reclient_bindir="/usr/local/reclient/bin",
                    bq_logs_table="project.dataset.reproxy_logs",
                    upload_batch_size=100,
                    dry_run=False,
                    verbose=False,
                    print_sample=False,
                )
        mock_convert_log.assert_called_once()
        mock_upload.assert_called_once()
        self.assertEqual(exit_code, 0)

    def test_mocked_upload_failure(self):
        with mock.patch.object(
                upload_reproxy_logs, "convert_reproxy_actions_log",
                return_value=self.fake_log()) as mock_convert_log:
            with mock.patch.object(upload_reproxy_logs,
                                   "bq_upload_remote_action_logs",
                                   return_value=1) as mock_upload:
                exit_code = upload_reproxy_logs.main_upload_logs(
                    uuid="feed-f00d-feed-f00d",
                    reproxy_logdir="/tmp/reproxy.log.dir",
                    reclient_bindir="/usr/local/reclient/bin",
                    bq_logs_table="project.dataset.reproxy_logs",
                    upload_batch_size=100,
                    dry_run=False,
                    verbose=False,
                    print_sample=False,
                )
        mock_convert_log.assert_called_once()
        mock_upload.assert_called_once()
        self.assertEqual(exit_code, 1)

    def test_empty_records(self):
        with mock.patch.object(
                upload_reproxy_logs, "convert_reproxy_actions_log",
                return_value=log_pb2.LogDump()) as mock_convert_log:
            with mock.patch.object(upload_reproxy_logs,
                                   "bq_upload_remote_action_logs",
                                   return_value=0) as mock_upload:
                exit_code = upload_reproxy_logs.main_upload_logs(
                    uuid="feed-f00d-feed-f00d",
                    reproxy_logdir="/tmp/reproxy.log.dir",
                    reclient_bindir="/usr/local/reclient/bin",
                    bq_logs_table="project.dataset.reproxy_logs",
                    upload_batch_size=100,
                    dry_run=False,
                    verbose=False,
                    print_sample=False,
                )
        mock_convert_log.assert_called_once()
        mock_upload.assert_not_called()
        self.assertEqual(exit_code, 0)


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
                    metrics_file="/tmp/reproxy.log.dir/rbe_metrics.pb",
                )
        mock_open.assert_called_once()
        mock_parse.assert_called_once()
        self.assertEqual(stats, stats_pb2.Stats())


class BQUploadRemoteActionLogsTest(unittest.TestCase):

    def test_batch_upload(self):
        bq_table = "proj.dataset.tablename"
        with mock.patch.object(subprocess, "call",
                               side_effect=[0, 0]) as mock_process_call:
            upload_reproxy_logs.bq_upload_remote_action_logs(
                records=[{
                    "records": []
                }] * 8,
                bq_table=bq_table,
                batch_size=4,
            )
        # Cannot use assert_called_with due to use of temporary file.
        # Mock is called twice due to batch size being half the size
        # of the number of records.
        mock_process_call.assert_called()


class BQUploadMetricsTest(unittest.TestCase):

    def test_upload(self):
        bq_table = "proj.dataset.tablename"
        with mock.patch.object(subprocess, "call",
                               return_value=0) as mock_process_call:
            upload_reproxy_logs.bq_upload_metrics(
                metrics=[{
                    "metrics": []
                }],
                bq_table=bq_table,
            )
        # Cannot use assert_called_with due to use of temporary file.
        mock_process_call.assert_called_once()


class MainSingleLogdirTest(ReproxyLogdirTestHarness):

    def test_build_not_done_yet(self):
        os.remove(self._metrics_file)  # cause this log dir to be skipped
        with mock.patch.object(upload_reproxy_logs,
                               "main_upload_metrics") as mock_upload_metrics:
            with mock.patch.object(upload_reproxy_logs,
                                   "main_upload_logs") as mock_upload_logs:
                exit_code = upload_reproxy_logs.main_single_logdir(
                    reproxy_logdir=self._reproxy_logdir,
                    reclient_bindir="/re-client/tools",
                    metrics_table="project:metrics.metrics_table",
                    logs_table="project:metrics.logs_table",
                    uuid_flag="feed-face",
                    upload_batch_size=10,
                    print_sample=False,
                    dry_run=False,
                    verbose=False,
                )
        mock_upload_metrics.assert_not_called()
        mock_upload_logs.assert_not_called()
        self.assertEqual(exit_code, 0)
        self.assertFalse(os.path.exists(self._stamp_file))

    def test_already_uploaded(self):
        self.touch_stamp_file()
        with mock.patch.object(upload_reproxy_logs,
                               "main_upload_metrics") as mock_upload_metrics:
            with mock.patch.object(upload_reproxy_logs,
                                   "main_upload_logs") as mock_upload_logs:
                upload_reproxy_logs.main_single_logdir(
                    reproxy_logdir=self._reproxy_logdir,
                    reclient_bindir="/re-client/tools",
                    metrics_table="project:metrics.metrics_table",
                    logs_table="project:metrics.logs_table",
                    uuid_flag="feed-face",
                    upload_batch_size=10,
                    print_sample=False,
                    dry_run=False,
                    verbose=False,
                )
        mock_upload_metrics.assert_not_called()
        mock_upload_logs.assert_not_called()

    def test_no_stamp_have_uuid_flag(self):
        with mock.patch.object(upload_reproxy_logs, "main_upload_metrics",
                               return_value=0) as mock_upload_metrics:
            with mock.patch.object(upload_reproxy_logs, "main_upload_logs",
                                   return_value=0) as mock_upload_logs:
                exit_code = upload_reproxy_logs.main_single_logdir(
                    reproxy_logdir=self._reproxy_logdir,
                    reclient_bindir="/re-client/tools",
                    metrics_table="project:metrics.metrics_table",
                    logs_table="project:metrics.logs_table",
                    uuid_flag="feed-face",
                    upload_batch_size=10,
                    print_sample=False,
                    dry_run=False,
                    verbose=False,
                )
        mock_upload_metrics.assert_called_once()
        mock_upload_logs.assert_called_once()
        self.assertTrue(os.path.isfile(self._stamp_file))
        self.assertEqual(exit_code, 0)

    def test_no_stamp_have_uuid_file(self):
        with open(self._build_id_file, "w") as build_id_file:
            build_id_file.write("feed-face\n")
        with mock.patch.object(upload_reproxy_logs, "main_upload_metrics",
                               return_value=0) as mock_upload_metrics:
            with mock.patch.object(upload_reproxy_logs, "main_upload_logs",
                                   return_value=0) as mock_upload_logs:
                exit_code = upload_reproxy_logs.main_single_logdir(
                    reproxy_logdir=self._reproxy_logdir,
                    reclient_bindir="/re-client/tools",
                    metrics_table="project:metrics.metrics_table",
                    logs_table="project:metrics.logs_table",
                    uuid_flag="",
                    upload_batch_size=10,
                    print_sample=False,
                    dry_run=False,
                    verbose=False,
                )
        mock_upload_metrics.assert_called_once()
        mock_upload_logs.assert_called_once()
        self.assertTrue(os.path.isfile(self._stamp_file))
        self.assertEqual(exit_code, 0)

    def test_no_stamp_auto_uuid(self):
        with mock.patch.object(upload_reproxy_logs, "main_upload_metrics",
                               return_value=0) as mock_upload_metrics:
            with mock.patch.object(upload_reproxy_logs, "main_upload_logs",
                                   return_value=0) as mock_upload_logs:
                upload_reproxy_logs.main_single_logdir(
                    reproxy_logdir=self._reproxy_logdir,
                    reclient_bindir="/re-client/tools",
                    metrics_table="project:metrics.metrics_table",
                    logs_table="project:metrics.logs_table",
                    uuid_flag="",
                    upload_batch_size=10,
                    print_sample=False,
                    dry_run=False,
                    verbose=False,
                )
        mock_upload_metrics.assert_called_once()
        mock_upload_logs.assert_called_once()
        self.assertTrue(os.path.isfile(self._stamp_file))
        # build_id is automatically generated
        self.assertTrue(os.path.isfile(self._build_id_file))

    def test_upload_metrics_error(self):
        with open(self._build_id_file, "w") as build_id_file:
            build_id_file.write("feed-face\n")
        with mock.patch.object(upload_reproxy_logs, "main_upload_metrics",
                               return_value=1) as mock_upload_metrics:
            exit_code = upload_reproxy_logs.main_single_logdir(
                reproxy_logdir=self._reproxy_logdir,
                reclient_bindir="/re-client/tools",
                metrics_table="project:metrics.metrics_table",
                logs_table="project:metrics.logs_table",
                uuid_flag="",
                upload_batch_size=10,
                print_sample=False,
                dry_run=False,
                verbose=False,
            )
        mock_upload_metrics.assert_called_once()
        self.assertFalse(os.path.exists(self._stamp_file))
        self.assertEqual(exit_code, 1)

    def test_upload_logs_error(self):
        with open(self._build_id_file, "w") as build_id_file:
            build_id_file.write("feed-face\n")
        with mock.patch.object(upload_reproxy_logs, "main_upload_logs",
                               return_value=1) as mock_upload_logs:
            with mock.patch.object(upload_reproxy_logs, "main_upload_metrics",
                                   return_value=0) as mock_upload_metrics:
                exit_code = upload_reproxy_logs.main_single_logdir(
                    reproxy_logdir=self._reproxy_logdir,
                    reclient_bindir="/re-client/tools",
                    metrics_table="project:metrics.metrics_table",
                    logs_table="project:metrics.logs_table",
                    uuid_flag="",
                    upload_batch_size=10,
                    print_sample=False,
                    dry_run=False,
                    verbose=False,
                )
        mock_upload_logs.assert_called_once()
        mock_upload_metrics.assert_called_once()
        self.assertFalse(os.path.exists(self._stamp_file))
        self.assertEqual(exit_code, 1)


if __name__ == '__main__':
    unittest.main()
