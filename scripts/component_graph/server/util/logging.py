#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Provides default logging configuration for the project. """

import logging


def get_logger(name):
    """ Returns a default logger configured a generic format """
    logging.basicConfig(
        level=logging.DEBUG, format='[%(levelname)s][%(name)s] - %(message)s')
    logger = logging.getLogger(name)
    return logger
