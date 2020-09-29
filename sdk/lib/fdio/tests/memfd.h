// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_TESTS_MEMFD_H_
#define LIB_FDIO_TESTS_MEMFD_H_

__BEGIN_CDECLS

int memfd_create(const char* name, unsigned int flags);

__END_CDECLS

#endif  // LIB_FDIO_TESTS_MEMFD_H_
