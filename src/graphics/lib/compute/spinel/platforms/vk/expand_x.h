// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXPAND_X_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXPAND_X_H_

//
// SPN_EXPAND(I,N,P,L):
//
//   INDEX, NEXT, PREV, LAST?
//

//
// clang-format off
//

#define SPN_EXPAND_0()                          \

#define SPN_EXPAND_1()                          \
  SPN_EXPAND_X(0, 1, 0,  true)

#define SPN_EXPAND_2()                          \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0,  true)

#define SPN_EXPAND_4()                          \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2,  true)

#define SPN_EXPAND_8()                          \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6,  true)

#define SPN_EXPAND_16()                         \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7, false)                  \
  SPN_EXPAND_X(9, 10,8, false)                  \
  SPN_EXPAND_X(10,11,9, false)                  \
  SPN_EXPAND_X(11,12,10,false)                  \
  SPN_EXPAND_X(12,13,11,false)                  \
  SPN_EXPAND_X(13,14,12,false)                  \
  SPN_EXPAND_X(14,15,13,false)                  \
  SPN_EXPAND_X(15,16,14, true)

#define SPN_EXPAND_32()                         \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7, false)                  \
  SPN_EXPAND_X(9, 10,8, false)                  \
  SPN_EXPAND_X(10,11,9, false)                  \
  SPN_EXPAND_X(11,12,10,false)                  \
  SPN_EXPAND_X(12,13,11,false)                  \
  SPN_EXPAND_X(13,14,12,false)                  \
  SPN_EXPAND_X(14,15,13,false)                  \
  SPN_EXPAND_X(15,16,14,false)                  \
  SPN_EXPAND_X(16,17,15,false)                  \
  SPN_EXPAND_X(17,18,16,false)                  \
  SPN_EXPAND_X(18,19,17,false)                  \
  SPN_EXPAND_X(19,20,18,false)                  \
  SPN_EXPAND_X(20,21,19,false)                  \
  SPN_EXPAND_X(21,22,20,false)                  \
  SPN_EXPAND_X(22,23,21,false)                  \
  SPN_EXPAND_X(23,24,22,false)                  \
  SPN_EXPAND_X(24,25,23,false)                  \
  SPN_EXPAND_X(25,26,24,false)                  \
  SPN_EXPAND_X(26,27,25,false)                  \
  SPN_EXPAND_X(27,28,26,false)                  \
  SPN_EXPAND_X(28,29,27,false)                  \
  SPN_EXPAND_X(29,30,28,false)                  \
  SPN_EXPAND_X(30,31,29,false)                  \
  SPN_EXPAND_X(31,32,30, true)

//
// Some non-power-of-2 expansions...
//

#define SPN_EXPAND_3()                          \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1,  true)

#define SPN_EXPAND_5()                          \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3,  true)

#define SPN_EXPAND_6()                          \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4,  true)

#define SPN_EXPAND_7()                          \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5,  true)

#define SPN_EXPAND_9()                          \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7,  true)

#define SPN_EXPAND_10()                         \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7, false)                  \
  SPN_EXPAND_X(9, 10,8,  true)

#define SPN_EXPAND_11()                         \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7, false)                  \
  SPN_EXPAND_X(9, 10,8, false)                  \
  SPN_EXPAND_X(10,11,9, true)

#define SPN_EXPAND_12()                         \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7, false)                  \
  SPN_EXPAND_X(9, 10,8, false)                  \
  SPN_EXPAND_X(10,11,9, false)                  \
  SPN_EXPAND_X(11,12,10, true)

#define SPN_EXPAND_13()                         \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7, false)                  \
  SPN_EXPAND_X(9, 10,8, false)                  \
  SPN_EXPAND_X(10,11,9, false)                  \
  SPN_EXPAND_X(11,12,10,false)                  \
  SPN_EXPAND_X(12,13,11, true)

#define SPN_EXPAND_14()                         \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7, false)                  \
  SPN_EXPAND_X(9, 10,8, false)                  \
  SPN_EXPAND_X(10,11,9, false)                  \
  SPN_EXPAND_X(11,12,10,false)                  \
  SPN_EXPAND_X(12,13,11,false)                  \
  SPN_EXPAND_X(13,14,12, true)

#define SPN_EXPAND_15()                         \
  SPN_EXPAND_X(0, 1, 0, false)                  \
  SPN_EXPAND_X(1, 2, 0, false)                  \
  SPN_EXPAND_X(2, 3, 1, false)                  \
  SPN_EXPAND_X(3, 4, 2, false)                  \
  SPN_EXPAND_X(4, 5, 3, false)                  \
  SPN_EXPAND_X(5, 6, 4, false)                  \
  SPN_EXPAND_X(6, 7, 5, false)                  \
  SPN_EXPAND_X(7, 8, 6, false)                  \
  SPN_EXPAND_X(8, 9, 7, false)                  \
  SPN_EXPAND_X(9, 10,8, false)                  \
  SPN_EXPAND_X(10,11,9, false)                  \
  SPN_EXPAND_X(11,12,10,false)                  \
  SPN_EXPAND_X(12,13,11,false)                  \
  SPN_EXPAND_X(13,14,12,false)                  \
  SPN_EXPAND_X(14,15,13, true)

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_EXPAND_X_H_
