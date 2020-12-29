// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

extern "C" {
  bool rs_test_trace_enabled(void);
  bool rs_test_category_disabled(void);
  bool rs_test_category_enabled(void);

  void rs_test_counter_macro(void);
  void rs_test_instant_macro(void);

  void rs_test_duration_macro(void);
  void rs_test_duration_macro_with_scope(void);
  void rs_test_duration_begin_end_macros(void);
  void rs_test_blob_macro(void);
  void rs_test_flow_begin_step_end_macros(void);

  void rs_test_arglimit(void);
}
