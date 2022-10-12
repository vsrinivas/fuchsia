#!/usr/bin/env fuchsia-vendored-python
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import sys
import importlib.util

loader = importlib.machinery.SourceFileLoader("embossc", sys.argv[1])
spec = importlib.util.spec_from_loader(loader.name, loader)
main_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(main_module)

sys.exit(main_module.main(sys.argv[1:]))
