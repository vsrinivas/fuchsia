// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <sys/types.h>
#include <stdlib.h>

__BEGIN_CDECLS

/* io port stuff */
int x86_set_io_bitmap(uint32_t port, uint32_t len, bool enable);

__END_CDECLS
