// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

struct compatibility_test_metadata {
  bool add_in_bind;
  bool remove_in_unbind;
  bool remove_twice_in_unbind;
  bool remove_in_suspend;
};
