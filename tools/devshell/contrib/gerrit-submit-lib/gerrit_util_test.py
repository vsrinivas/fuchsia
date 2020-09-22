#!/usr/bin/python3
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import http.server
import threading
from unittest import mock

from typing import List, Optional

import gerrit_util


# Implements logic to bring up a simple HTTP server that responds to a single JSON
# request and shuts down again.
class JsonResponder:

  def __init__(self, response: bytes):
    self.response: bytes = response
    self.got_request: bool = False
    self.url: Optional[str] = None

    # Start up a simple HTTP server running on its own thread.
    self._server = http.server.HTTPServer(('localhost', 0), self._make_handler())
    self._server_thread = threading.Thread(target=self._server.serve_forever, args=())
    self._server_thread.daemon = True
    self._server_thread.start()
    self.port = self._server.server_port

  def __del__(self):
    self._server.shutdown()
    self._server_thread.join()

  def _make_handler(self):
    # Give access to "self" in the new Handler class under the name "parent".
    parent = self

    # Create a Handler class that, when instantiated, responds to GET and POST requests
    # with the given response.
    class Handler(http.server.BaseHTTPRequestHandler):
      def do_POST(self) -> None:
        self.send_response(200)
        self.send_header('Content-type', 'javascript/json')
        self.end_headers()
        self.wfile.write(b")]}'\n")  # Write the JSON header.
        self.wfile.write(parent.response)
        parent.url = self.path
    return Handler


@mock.patch('gerrit_util.GERRIT_PROTOCOL', 'http')
class TestGerritUtil(unittest.TestCase):

  def test_post_json(self) -> None:
    # Test plumbing through GerritUtil to a HTTP server and back again.
    responder = JsonResponder(b'{"labels": {"Commit-Queue": 2}}')
    gerrit_util.SetReview(
        'localhost:%d' % responder.port, '12345', labels={'Commit-Queue': 2}, notify=False)


if __name__ == '__main__':
  unittest.main()
