// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_MACROS_H_
#define LIB_FIDL_CPP_BINDINGS_MACROS_H_

// Used to make a type move-only. The MoveOnlyTypeForCPP03 typedef is for
// fidl::Array to tell that this type is move-only. This is typically used like:
//
//   class MyMoveOnlyClass {
//    public:
//     ...
//    private:
//     ...
//     FIDL_MOVE_ONLY_TYPE(MyMoveOnlyClass);
//   };
//
// (Note: Class members following the use of this macro will have private access
// by default.)
#define FIDL_MOVE_ONLY_TYPE(type)    \
 public:                             \
  typedef void MoveOnlyTypeForCPP03; \
                                     \
 private:                            \
  type(const type&) = delete;        \
  void operator=(const type&) = delete

#endif  // LIB_FIDL_CPP_BINDINGS_MACROS_H_
