// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <compiler.h>
#include <kernel/thread.h>

__BEGIN_CDECLS

void fpu_init(void);
void fpu_init_thread_states(thread_t *t);
void fpu_context_switch(thread_t *old_thread, thread_t *new_thread);

/* called during a #NA exception */
void fpu_dev_na_handler(void);

__END_CDECLS

/* End of file */
