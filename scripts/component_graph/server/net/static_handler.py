#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Static handler servers static pages from the disk.

The Static handler is a trivial static file retriever for an allow list of
static files. This is intended to be used on development servers and not the
public internet. It defines a limited set of files it serves which prevents
walking around the hosts filesystem.
"""

from server.util.logging import get_logger
import os

class StaticHandler:
  """ Serves static pages from an allow list. """
  def __init__(self, fuchsia_root):
    self.logger = get_logger(__name__)
    self.static_root = os.path.join(fuchsia_root, "scripts/component_graph/server/static/")
    self.static_lib_path = os.path.join(fuchsia_root, "scripts/third_party/d3/d3.js")
    self.static_files = [
        {
            "type": "text/javascript",
            "path": "js/main.js"
        },
        {
            "type": "text/css",
            "path": "css/main.css"
        },
        {
            "type": "image/png",
            "path": "img/logo.png"
        },
        {
            "type": "image/svg+xml",
            "path": "img/logo.svg"
        },
    ]

  def respond(self, path):
    """Only respond to known absolute paths ignoring everything else.

    Since this runs on developer machines this is to mitigate the risk of
    accidental directory enumeration through URLs.
    """
    try:
      if path == "/":
        return {
            "type": "text/html",
            "data": open(os.path.join(self.static_root, "index.html"), "r").read().encode()
        }
      file_info_matches = [
          f for f in self.static_files if "/static/" + f["path"] == path
      ]
      if len(file_info_matches) == 1:
        file_info = file_info_matches[0]
        host_path = os.path.join(self.static_root, file_info["path"])
        if file_info["type"].startswith("image"):
          return {"type": file_info["type"],
                  "data": open(host_path, "rb").read()}
        return {"type": file_info["type"],
                "data": open(host_path, "r").read().encode()}
      if path == "/static/js/d3.js":
        return {"type": "text/javascript",
                "data": open(self.static_lib_path, "r").read().encode()}

    except FileNotFoundError:
      pass
    return None
