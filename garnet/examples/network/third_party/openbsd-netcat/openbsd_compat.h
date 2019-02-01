// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

long long strtonum(const char *nptr, long long minval, long long maxval,
                   const char **errstr);
void errc(int eval, int code, const char* fmt, ...);
unsigned int arc4random(void);
