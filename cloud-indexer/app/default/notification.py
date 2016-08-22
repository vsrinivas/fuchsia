# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Handles indexing on the Modular CDN."""

import json
import logging
import os
import re
import webapp2

from google.appengine.api import taskqueue


class MainPage(webapp2.RequestHandler):
  """Processes notification events."""
  PATTERN = re.compile(r'^services/(?P<arch>[^/]+)/(?P<revision>[^/]+)'
                       r'/[^/]+.yaml$')

  def get(self):
    self.response.status = 404
    return

  def post(self):
    """Processes the notification event.

    Object change notifications can be identified by their X-Goog-Resource-State
    headers. From there, we issue the status code 200 to acknowledge the
    notification. We use a task queue as a concurrency mechanism to ensure that
    only a single index is being updated at a time.
    """

    # TODO(victorkwan): Additionally, we should provide a ClientToken.
    if 'X-Goog-Resource-State' not in self.request.headers:
      logging.info('POST request received with incorrect header.')
      self.response.status = 400
      return

    resource_state = self.request.headers['X-Goog-Resource-State']
    if resource_state == 'sync':
      # A sync message indicates that a watch request has been issued for a
      # bucket, which is specified by the X-Goog-Resource-Uri header.
      logging.info('Sync message received for URI: %s',
                   self.request.headers['X-Goog-Resource-Uri'])
      self.response.status = 200
      return
    else:
      change_notification = json.loads(self.request.body)
      bucket = change_notification['bucket']

      if bucket != os.environ.get('BUCKET_NAME'):
        logging.info('Non-Modular notification received. Bailing out.')
        self.response.status = 200
        return

      name = change_notification['name']
      match = MainPage.PATTERN.match(name)
      if match is None:
        logging.info('Non-manifest file added: %s', name)
        self.response.status = 200
        return

      result = match.groupdict()
      result['name'] = name
      result['resource_state'] = resource_state

      logging.info('Enqueued manifest at: %s', name)
      taskqueue.Task(
          method='POST',
          url='/notification_handler/',
          payload=json.dumps(result)).add(queue_name='indexing')
      self.response.status = 200
      return

debug = os.environ.get('SERVER_SOFTWARE', '').startswith('Dev')
app = webapp2.WSGIApplication([('/', MainPage)], debug=debug)
