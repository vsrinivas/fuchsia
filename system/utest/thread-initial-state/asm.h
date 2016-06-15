// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __ASM_H
#define __ASM_H

#define FUNCTION(x)    \
    .global x;         \
    .type x, STT_FUNC; \
    x:
#define DATA(x)          \
    .global x;           \
    .type x, STT_OBJECT; \
    x:

#define LOCAL_FUNCTION(x) \
    .type x, STT_FUNC;    \
    x:
#define LOCAL_DATA(x)    \
    .type x, STT_OBJECT; \
    x:

#define END(x) .size x, .- x

#endif
