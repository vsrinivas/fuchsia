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

#ifndef __LIB_FONT_H
#define __LIB_FONT_H

#include <gfx/gfx.h>

#define SMALL_FONT 1

#if SMALL_FONT
#define FONT_X 9
#define FONT_Y 16
#else
#define FONT_X 18
#define FONT_Y 32
#endif

void font_draw_char(gfx_surface *surface, unsigned char c, int x, int y, uint32_t color, uint32_t bgcolor);

#endif

