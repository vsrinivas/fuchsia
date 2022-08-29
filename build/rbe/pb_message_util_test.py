#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
from unittest import mock

import pb_message_util

# The function being tested works on any subclass of message.Message
# which is an abstract base class.  However, we need a concrete message type
# to test.  We choose api.proxy.log_pb2.LogDump because this is what
# upload_reproxy_logs.py uses, and it is very stable.
from api.proxy import log_pb2
from go.api.command import command_pb2
from google.protobuf import timestamp_pb2


class ProtoMessageToBQDictTest(unittest.TestCase):

    def test_empty_log(self):
        log = log_pb2.LogDump()
        converted_log = pb_message_util.proto_message_to_bq_dict(log)
        self.assertEqual(str(converted_log), "{}")

    def test_one_record(self):
        log = log_pb2.LogDump(records=[log_pb2.LogRecord()])
        converted_log = pb_message_util.proto_message_to_bq_dict(log)
        self.assertEqual(str(converted_log), "{'records': [{}]}")

    def test_two_records(self):
        log = log_pb2.LogDump(
            records=[log_pb2.LogRecord(),
                     log_pb2.LogRecord()])
        converted_log = pb_message_util.proto_message_to_bq_dict(log)
        self.assertEqual(str(converted_log), "{'records': [{}, {}]}")

    def test_nested_message_strings(self):
        log = log_pb2.LogDump(
            records=[
                log_pb2.LogRecord(
                    command=command_pb2.Command(
                        exec_root="/home",
                        args=["echo", "hello"],
                    ))
            ])
        converted_log = pb_message_util.proto_message_to_bq_dict(log)
        self.assertEqual(
            str(converted_log),
            "{'records': [{'command': {'exec_root': '/home', 'args': ['echo', 'hello']}}]}"
        )

    def test_nested_message_map_string_string(self):
        """Exercises protobuf.containers.ScalarMap"""
        log = log_pb2.LogDump(
            records=[
                log_pb2.LogRecord(
                    command=command_pb2.Command(
                        platform={
                            "foo": "bar",
                            "baz": "quux"
                        },
                    ))
            ])
        converted_log = pb_message_util.proto_message_to_bq_dict(log)
        self.assertEqual(
            str(converted_log),
            "{'records': [{'command': {'platform': [{'key': 'baz', 'value': 'quux'}, {'key': 'foo', 'value': 'bar'}]}}]}"
        )

    def test_nested_message_map_string_message(self):
        """Exercises protobuf.containers.MessageMap"""
        log = log_pb2.LogDump(
            records=[
                log_pb2.LogRecord(
                    remote_metadata=log_pb2.RemoteMetadata(
                        event_times={
                            "birth":
                                command_pb2.TimeInterval(
                                    to=timestamp_pb2.Timestamp(
                                        seconds=1601428537)),
                            "death":
                                command_pb2.TimeInterval(
                                    to=timestamp_pb2.Timestamp(
                                        seconds=1601439840))
                        },
                    ))
            ])
        converted_log = pb_message_util.proto_message_to_bq_dict(log)
        self.assertEqual(
            str(converted_log),
            "{'records': [{'remote_metadata': {'event_times': [{'key': 'birth', 'value': {'to': '2020-09-30T01:15:37Z'}}, {'key': 'death', 'value': {'to': '2020-09-30T04:24:00Z'}}]}}]}"
        )

    def test_nested_message_enum(self):
        """Make sure enum name (not number) is printed."""
        log = log_pb2.LogDump(
            records=[
                log_pb2.LogRecord(
                    completion_status=log_pb2.CompletionStatus.STATUS_CACHE_HIT)
            ])
        converted_log = pb_message_util.proto_message_to_bq_dict(log)
        self.assertEqual(
            str(converted_log),
            "{'records': [{'completion_status': 'STATUS_CACHE_HIT'}]}")

    def test_timestamp(self):
        """Make sure timestamp is formatted, and not (seconds, nanos)"""
        log = command_pb2.TimeInterval(
            to=timestamp_pb2.Timestamp(seconds=1661472513, nanos=114242156))
        converted_log = pb_message_util.proto_message_to_bq_dict(log)
        self.assertEqual(
            str(converted_log), "{'to': '2022-08-26T00:08:33.114242156Z'}")


if __name__ == '__main__':
    unittest.main()
