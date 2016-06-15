// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if !defined(__ALLOCA_H)
#define __ALLOCA_H

#define alloca(size) __builtin_alloca (size)

#endif  /* !__ALLOCA_H */
