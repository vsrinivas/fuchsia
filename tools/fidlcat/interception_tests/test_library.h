// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_INTERCEPTION_TESTS_TEST_LIBRARY_H_
#define TOOLS_FIDLCAT_INTERCEPTION_TESTS_TEST_LIBRARY_H_

namespace fidl_codec {
class LibraryLoader;
}

fidl_codec::LibraryLoader* GetTestLibraryLoader();

#endif  // TOOLS_FIDLCAT_INTERCEPTION_TESTS_TEST_LIBRARY_H_
