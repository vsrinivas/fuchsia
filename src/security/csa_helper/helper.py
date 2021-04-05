#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Provides some basic helper functions for printing."""


def yellow(message):
  return '\033[1;33;40m%s\033[0m'%(message)


def white(message):
  return '\033[37;1;40m%s\033[0m'%(message)


def red(message):
  return '\033[1;31;40m%s\033[0m'%(message)
