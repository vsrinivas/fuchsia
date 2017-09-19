#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to download Cobalt's report client binary from Google Cloud Storage.

We detect the current platform and download the appropriate binary.
We compute the sha1 of the downloaded tar file and check it against the
expected sha1.
"""

import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile

CURRENT_VERSION_LINUX64='912e117fbb3a752d61c8320d9df934ddc4dec600'

BUCKET_PREFIX="https://storage.googleapis.com/fuchsia-build/" \
              "cobalt/report_client"

def _platform_string():
  return '%s%s' % (platform.system().lower(), platform.architecture()[0][:2])

def _download_tgz(platform_string, current_version, download_file_name):
  bucket_uri = '%s/%s/%s' % (BUCKET_PREFIX, platform_string, current_version)
  cmd = ['curl', '-o', download_file_name, bucket_uri]
  subprocess.check_call(cmd)

def _check_sha1(temp_tgz_file):
  cmd = ['sha1sum', temp_tgz_file]
  SHA_LENGTH=40
  sha1 = subprocess.check_output(cmd)[:SHA_LENGTH]
  if sha1 == CURRENT_VERSION_LINUX64:
    print "Sha1 hash verified."
    return True
  else:
    print "WARNING: Sha1 hash of downloaded file is incorrect!"
    return False

def _untar(file_name):
  cmd = ['tar', '-zxf', file_name]
  subprocess.check_call(cmd)

def main():
  platform_string =_platform_string()
  if platform_string == 'linux64':
    current_version = CURRENT_VERSION_LINUX64
  else:
    print ("We do not have a pre-built binary for your platform: %s" %
           platform_string)
    return 1

  download_file_name = 'report_client.%s.tgz' % platform_string
  _download_tgz(platform_string, current_version, download_file_name)
  if not _check_sha1(download_file_name):
    return 1
  _untar(download_file_name)
  os.remove(download_file_name)

if __name__ == '__main__':
  main()
