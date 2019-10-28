#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Link implementation for the ComponentGraph.

ComponentLink stores rich data that is exported to the component graph about
the type of link and the fidl service this link refers to.
"""


class ComponentLink:
    """ Trivial link between these two sides """

    def __init__(self, source, target, route_type, fidl_service):
        # The source of the request.
        self.source = source
        # The target to service this request.
        self.target = target
        self.route_type = route_type
        self.fidl_service = fidl_service

    def export(self):
        """ Exports the link into JSON compatible python. """
        return {
            "source": self.source,
            "target": self.target,
            "type": self.route_type,
            "fidl_service": self.fidl_service,
        }
