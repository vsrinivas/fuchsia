# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for notification service."""

import json
import notification
import os
import unittest
import webapp2

from google.appengine.ext import testbed


class Environ(object):
  """Helper class that stubs and restores an environment key-value pair."""

  def __init__(self, key, value):
    if key in os.environ:
      self._old_value = os.environ[key]
    self._key = key
    os.environ[key] = value

  def __enter__(self):
    return

  def __exit__(self, exception_type, exception_value, exception_traceback):
    if self._old_value is not None:
      os.environ[self._key] = self._old_value
    else:
      del os.environ[self._key]


class MainPageTestCase(unittest.TestCase):
  """Test cases for notification processing."""
  TEST_BUCKET_NAME = 'modular'

  def setUp(self):
    self.testbed = testbed.Testbed()
    self.testbed.activate()
    self.testbed.init_taskqueue_stub(
        root_path=os.path.join(os.path.dirname(__file__), '../..'))
    self.taskqueue_stub = self.testbed.get_stub(testbed.TASKQUEUE_SERVICE_NAME)

  def tearDown(self):
    self.testbed.deactivate()

  def create_notification(self, resource_state, bucket=None, name=None):
    """Helper method for creating notification requests."""
    request = webapp2.Request.blank('/')
    request.method = 'POST'
    request.headers['X-Goog-Resource-State'] = resource_state
    if bucket is not None and name is not None:
      request.body = json.dumps({
          'bucket': bucket,
          'name': name
      })
    return request

  def test_get(self):
    request = webapp2.Request.blank('/')
    response = request.get_response(notification.app)
    self.assertEqual(response.status_code, 404)

  def test_post_non_notification(self):
    request = webapp2.Request.blank('/')
    request.method = 'POST'
    response = request.get_response(notification.app)
    self.assertEqual(response.status_code, 400)

  def test_post_non_modular(self):
    request = self.create_notification('exists', 'improbable_bucket',
                                       '/service/path/to/file.yaml')
    with Environ('BUCKET_NAME', MainPageTestCase.TEST_BUCKET_NAME):
      response = request.get_response(notification.app)

    # In the case that we receive a non-modular notification, we send OK so that
    # the service does not continue to attempt to send notifications.
    self.assertEqual(response.status_code, 200)

  def test_post_non_manifest(self):
    request = self.create_notification('not_exists', 'modular',
                                       '/services/path/to/file.txt')
    with Environ('BUCKET_NAME', MainPageTestCase.TEST_BUCKET_NAME):
      response = request.get_response(notification.app)

    # Likewise, we ignore all notifications relating to non-manifest files.
    self.assertEqual(response.status_code, 200)

  def test_post_manifest(self):
    request = self.create_notification('exists', 'modular',
                                       'services/android/abc123/lasagna.yaml')
    with Environ('BUCKET_NAME', MainPageTestCase.TEST_BUCKET_NAME):
      response = request.get_response(notification.app)
    self.assertEqual(response.status_code, 200)

    # First, we check that the task is correctly in the indexing queue.
    tasks = self.taskqueue_stub.get_filtered_tasks(queue_names='indexing')
    self.assertEquals(len(tasks), 1)

    body = json.loads(tasks[0].payload)
    self.assertEquals(tasks[0].url, '/notification_handler/')
    self.assertEquals(body['name'], 'services/android/abc123/lasagna.yaml')
    self.assertEquals(body['resource_state'], 'exists')
    self.assertEquals(body['arch'], 'android')
    self.assertEquals(body['revision'], 'abc123')
