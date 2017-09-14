// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <stdbool.h>

__BEGIN_CDECLS

bool cwriter_test_string_registration_and_retrieval(void);

bool cwriter_test_bulk_string_registration_and_retrieval(void);

bool cwriter_test_thread_registration(void);

bool cwriter_test_bulk_thread_registration(void);

bool cwriter_test_event_writing(void);

bool cwriter_test_event_writing_multithreaded(void);

bool cwriter_test_event_writing_with_arguments(void);

bool cwriter_test_event_writing_with_arguments_multithreaded(void);

__END_CDECLS
