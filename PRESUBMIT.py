# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Presubmit script for modular.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts for
details on the presubmit API built into depot_tools.
"""

def _CheckDartBindings(input_api, output_api):
  """Check that generated .mojom.dart files are current"""
  args = [input_api.python_executable,
          'modular_tools/presubmit/check_mojom_dart.py',
          '--affected-files']
  files = []
  for f in input_api.AffectedFiles():
    files.append(f.LocalPath())
  args.extend(files)
  try:
    input_api.subprocess.check_output(args)
    return []
  except input_api.subprocess.CalledProcessError, error:
    return [output_api.PresubmitError(
        'Dart bindings need to be updated.',
        long_text=error.output)]

def CheckChangeOnUpload(input_api, output_api):
  results = []
  results.extend(_CheckDartBindings(input_api, output_api))
  return results

def CheckChangeOnCommit(input_api, output_api):
  results = []
  results.extend(_CheckDartBindings(input_api, output_api))
  return results
