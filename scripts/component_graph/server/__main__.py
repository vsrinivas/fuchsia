#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Entry point to start the server.

Main will launch a root handler on port 8080 by default and start serving static
files
and api requests. This is not designed to run on the public internet and is
instead designed to be a development tool used to analyze the state of the
system.
"""

from http.server import HTTPServer, SimpleHTTPRequestHandler
from server.fpm import PackageManager
from server.net import ApiHandler
import server.util
import argparse


def RootRequestHandlerFactory(package_server_url):
    """The RootRequestHandlerFactory is responsible for injecting the package server into a generated RootRequestHandler.

  This is because the base http.server only accepts a class for
  construction.
  """

    class RootRequestHandler(SimpleHTTPRequestHandler):
        """Forwards requests to the static or api request handler if the path is defined correctly.

    Otherwise 404 is returned.
    """
        api_handler = ApiHandler(
            PackageManager(
                package_server_url, server.util.env.get_fuchsia_root()))

        def handle_api_request(self):
            """ Responds to JSON API requests """
            response = self.api_handler.respond(self.path)
            if response:
                self.send_response(200)
                self.send_header("Content-type", "application/json")
                self.end_headers()
                self.wfile.write(response.encode())
            else:
                self.send_response(404)

        def do_GET(self):
            """ Root handler that forwards requests to sub handlers """
            if self.path.startswith("/api/"):
                return self.handle_api_request()
            return self.send_response(404)

    return RootRequestHandler


def main(args):
    """Constructs the HTTP server and starts handling requests."""
    logger = server.util.logging.get_logger("ComponentGraph")
    logger.info(
        "Starting Component Graph at %s", "http://0.0.0.0:{}/".format(
            args.port))
    logger.info(
        "Connecting to Fuchsia Package Manager Server at %s",
        args.package_server)
    httpd = HTTPServer(
        ("0.0.0.0", args.port), RootRequestHandlerFactory(args.package_server))
    httpd.serve_forever()


if __name__ == "__main__":
    arg_parser = argparse.ArgumentParser(
        description="Fuchsia component graph server.")
    arg_parser.add_argument(
        "--port", type=int, default=8080, help="Port to run server on")
    arg_parser.add_argument(
        "--package-server",
        type=str,
        default="http://0.0.0.0:8083",
        help="Package server to get packages from")
    main(arg_parser.parse_args())
