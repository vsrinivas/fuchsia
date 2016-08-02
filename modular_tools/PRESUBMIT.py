# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Presubmit script for modular_tools.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts for
details on the presubmit API built into depot_tools.
"""


def _CheckPylintClean(input_api, output_api):
  white_list = ['.*\.py$',  # Match any *.py file.
                'deploy$',   # And a handful of Python tools without extension.
                'provision_device$',
                'roll_mojo$',
                'run$', ]
  return input_api.canned_checks.RunPylint(input_api, output_api,
                                           white_list=white_list)


def CheckChangeOnUpload(input_api, output_api):
  results = []
  results.extend(_CheckPylintClean(input_api, output_api))
  return results
