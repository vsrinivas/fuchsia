# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os

parser = argparse.ArgumentParser()
parser.add_argument("output_file")
parser.add_argument("input_file")

args = parser.parse_args()

input_file = json.load(open(args.input_file, "r"))
old_path = input_file["layer"]["library_path"]
# json is in data/vulkan/explicit_layer.d and .so is in lib/.
input_file["layer"]["library_path"] = os.path.join("../../../lib/", old_path)
open(args.output_file, "w").write(json.dumps(input_file))
