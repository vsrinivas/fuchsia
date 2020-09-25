// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// TODO(allanmac): (fxbug.dev/25519)
//
// The hardcoded "lion cub" will be replaced with a minimalist SVG
// parser and this file will be removed.
//

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/macros.h"
#include "ext/color/color.h"
#include "ext/transform_stack/transform_stack.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_opcodes.h"
#include "spinel/spinel_vk.h"

//
//
//

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

spn_path_t *
lion_cub_paths(spn_path_builder_t pb, uint32_t * const path_count)
{
  uint32_t     path_idx = 0;
  spn_path_t * paths    = malloc(sizeof(*paths) * 256);

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 69, 18));
    spn(path_builder_line_to(pb, 82, 8));
    spn(path_builder_line_to(pb, 99, 3));
    spn(path_builder_line_to(pb, 118, 5));
    spn(path_builder_line_to(pb, 135, 12));
    spn(path_builder_line_to(pb, 149, 21));
    spn(path_builder_line_to(pb, 156, 13));
    spn(path_builder_line_to(pb, 165, 9));
    spn(path_builder_line_to(pb, 177, 13));
    spn(path_builder_line_to(pb, 183, 28));
    spn(path_builder_line_to(pb, 180, 50));
    spn(path_builder_line_to(pb, 164, 91));
    spn(path_builder_line_to(pb, 155, 107));
    spn(path_builder_line_to(pb, 154, 114));
    spn(path_builder_line_to(pb, 151, 121));
    spn(path_builder_line_to(pb, 141, 127));
    spn(path_builder_line_to(pb, 139, 136));
    spn(path_builder_line_to(pb, 155, 206));
    spn(path_builder_line_to(pb, 157, 251));
    spn(path_builder_line_to(pb, 126, 342));
    spn(path_builder_line_to(pb, 133, 357));
    spn(path_builder_line_to(pb, 128, 376));
    spn(path_builder_line_to(pb, 83, 376));
    spn(path_builder_line_to(pb, 75, 368));
    spn(path_builder_line_to(pb, 67, 350));
    spn(path_builder_line_to(pb, 61, 350));
    spn(path_builder_line_to(pb, 53, 369));
    spn(path_builder_line_to(pb, 4, 369));
    spn(path_builder_line_to(pb, 2, 361));
    spn(path_builder_line_to(pb, 5, 354));
    spn(path_builder_line_to(pb, 12, 342));
    spn(path_builder_line_to(pb, 16, 321));
    spn(path_builder_line_to(pb, 4, 257));
    spn(path_builder_line_to(pb, 4, 244));
    spn(path_builder_line_to(pb, 7, 218));
    spn(path_builder_line_to(pb, 9, 179));
    spn(path_builder_line_to(pb, 26, 127));
    spn(path_builder_line_to(pb, 43, 93));
    spn(path_builder_line_to(pb, 32, 77));
    spn(path_builder_line_to(pb, 30, 70));
    spn(path_builder_line_to(pb, 24, 67));
    spn(path_builder_line_to(pb, 16, 49));
    spn(path_builder_line_to(pb, 17, 35));
    spn(path_builder_line_to(pb, 18, 23));
    spn(path_builder_line_to(pb, 30, 12));
    spn(path_builder_line_to(pb, 40, 7));
    spn(path_builder_line_to(pb, 53, 7));
    spn(path_builder_line_to(pb, 62, 12));
    spn(path_builder_line_to(pb, 69, 18));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 142, 79));
    spn(path_builder_line_to(pb, 136, 74));
    spn(path_builder_line_to(pb, 138, 82));
    spn(path_builder_line_to(pb, 133, 78));
    spn(path_builder_line_to(pb, 133, 84));
    spn(path_builder_line_to(pb, 127, 78));
    spn(path_builder_line_to(pb, 128, 85));
    spn(path_builder_line_to(pb, 124, 80));
    spn(path_builder_line_to(pb, 125, 87));
    spn(path_builder_line_to(pb, 119, 82));
    spn(path_builder_line_to(pb, 119, 90));
    spn(path_builder_line_to(pb, 125, 99));
    spn(path_builder_line_to(pb, 125, 96));
    spn(path_builder_line_to(pb, 128, 100));
    spn(path_builder_line_to(pb, 128, 94));
    spn(path_builder_line_to(pb, 131, 98));
    spn(path_builder_line_to(pb, 132, 93));
    spn(path_builder_line_to(pb, 135, 97));
    spn(path_builder_line_to(pb, 136, 93));
    spn(path_builder_line_to(pb, 138, 97));
    spn(path_builder_line_to(pb, 139, 94));
    spn(path_builder_line_to(pb, 141, 98));
    spn(path_builder_line_to(pb, 143, 94));
    spn(path_builder_line_to(pb, 144, 85));
    spn(path_builder_line_to(pb, 142, 79));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 127, 101));
    spn(path_builder_line_to(pb, 132, 100));
    spn(path_builder_line_to(pb, 137, 99));
    spn(path_builder_line_to(pb, 144, 101));
    spn(path_builder_line_to(pb, 143, 105));
    spn(path_builder_line_to(pb, 135, 110));
    spn(path_builder_line_to(pb, 127, 101));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 178, 229));
    spn(path_builder_line_to(pb, 157, 248));
    spn(path_builder_line_to(pb, 139, 296));
    spn(path_builder_line_to(pb, 126, 349));
    spn(path_builder_line_to(pb, 137, 356));
    spn(path_builder_line_to(pb, 158, 357));
    spn(path_builder_line_to(pb, 183, 342));
    spn(path_builder_line_to(pb, 212, 332));
    spn(path_builder_line_to(pb, 235, 288));
    spn(path_builder_line_to(pb, 235, 261));
    spn(path_builder_line_to(pb, 228, 252));
    spn(path_builder_line_to(pb, 212, 250));
    spn(path_builder_line_to(pb, 188, 251));
    spn(path_builder_line_to(pb, 178, 229));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------
  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 56, 229));
    spn(path_builder_line_to(pb, 48, 241));
    spn(path_builder_line_to(pb, 48, 250));
    spn(path_builder_line_to(pb, 57, 281));
    spn(path_builder_line_to(pb, 63, 325));
    spn(path_builder_line_to(pb, 71, 338));
    spn(path_builder_line_to(pb, 81, 315));
    spn(path_builder_line_to(pb, 76, 321));
    spn(path_builder_line_to(pb, 79, 311));
    spn(path_builder_line_to(pb, 83, 301));
    spn(path_builder_line_to(pb, 75, 308));
    spn(path_builder_line_to(pb, 80, 298));
    spn(path_builder_line_to(pb, 73, 303));
    spn(path_builder_line_to(pb, 76, 296));
    spn(path_builder_line_to(pb, 71, 298));
    spn(path_builder_line_to(pb, 74, 292));
    spn(path_builder_line_to(pb, 69, 293));
    spn(path_builder_line_to(pb, 74, 284));
    spn(path_builder_line_to(pb, 78, 278));
    spn(path_builder_line_to(pb, 71, 278));
    spn(path_builder_line_to(pb, 74, 274));
    spn(path_builder_line_to(pb, 68, 273));
    spn(path_builder_line_to(pb, 70, 268));
    spn(path_builder_line_to(pb, 66, 267));
    spn(path_builder_line_to(pb, 68, 261));
    spn(path_builder_line_to(pb, 60, 266));
    spn(path_builder_line_to(pb, 62, 259));
    spn(path_builder_line_to(pb, 65, 253));
    spn(path_builder_line_to(pb, 57, 258));
    spn(path_builder_line_to(pb, 59, 251));
    spn(path_builder_line_to(pb, 55, 254));
    spn(path_builder_line_to(pb, 55, 248));
    spn(path_builder_line_to(pb, 60, 237));
    spn(path_builder_line_to(pb, 54, 240));
    spn(path_builder_line_to(pb, 58, 234));
    spn(path_builder_line_to(pb, 54, 236));
    spn(path_builder_line_to(pb, 56, 229));

    spn(path_builder_move_to(pb, 74, 363));
    spn(path_builder_line_to(pb, 79, 368));
    spn(path_builder_line_to(pb, 81, 368));
    spn(path_builder_line_to(pb, 85, 362));
    spn(path_builder_line_to(pb, 89, 363));
    spn(path_builder_line_to(pb, 92, 370));
    spn(path_builder_line_to(pb, 96, 373));
    spn(path_builder_line_to(pb, 101, 372));
    spn(path_builder_line_to(pb, 108, 361));
    spn(path_builder_line_to(pb, 110, 371));
    spn(path_builder_line_to(pb, 113, 373));
    spn(path_builder_line_to(pb, 116, 371));
    spn(path_builder_line_to(pb, 120, 358));
    spn(path_builder_line_to(pb, 122, 363));
    spn(path_builder_line_to(pb, 123, 371));
    spn(path_builder_line_to(pb, 126, 371));
    spn(path_builder_line_to(pb, 129, 367));
    spn(path_builder_line_to(pb, 132, 357));
    spn(path_builder_line_to(pb, 135, 361));
    spn(path_builder_line_to(pb, 130, 376));
    spn(path_builder_line_to(pb, 127, 377));
    spn(path_builder_line_to(pb, 94, 378));
    spn(path_builder_line_to(pb, 84, 376));
    spn(path_builder_line_to(pb, 76, 371));
    spn(path_builder_line_to(pb, 74, 363));

    spn(path_builder_move_to(pb, 212, 250));
    spn(path_builder_line_to(pb, 219, 251));
    spn(path_builder_line_to(pb, 228, 258));
    spn(path_builder_line_to(pb, 236, 270));
    spn(path_builder_line_to(pb, 235, 287));
    spn(path_builder_line_to(pb, 225, 304));
    spn(path_builder_line_to(pb, 205, 332));
    spn(path_builder_line_to(pb, 177, 343));
    spn(path_builder_line_to(pb, 171, 352));
    spn(path_builder_line_to(pb, 158, 357));
    spn(path_builder_line_to(pb, 166, 352));
    spn(path_builder_line_to(pb, 168, 346));
    spn(path_builder_line_to(pb, 168, 339));
    spn(path_builder_line_to(pb, 165, 333));
    spn(path_builder_line_to(pb, 155, 327));
    spn(path_builder_line_to(pb, 155, 323));
    spn(path_builder_line_to(pb, 161, 320));
    spn(path_builder_line_to(pb, 165, 316));
    spn(path_builder_line_to(pb, 169, 316));
    spn(path_builder_line_to(pb, 167, 312));
    spn(path_builder_line_to(pb, 171, 313));
    spn(path_builder_line_to(pb, 168, 308));
    spn(path_builder_line_to(pb, 173, 309));
    spn(path_builder_line_to(pb, 170, 306));
    spn(path_builder_line_to(pb, 177, 306));
    spn(path_builder_line_to(pb, 175, 308));
    spn(path_builder_line_to(pb, 177, 311));
    spn(path_builder_line_to(pb, 174, 311));
    spn(path_builder_line_to(pb, 176, 316));
    spn(path_builder_line_to(pb, 171, 315));
    spn(path_builder_line_to(pb, 174, 319));
    spn(path_builder_line_to(pb, 168, 320));
    spn(path_builder_line_to(pb, 168, 323));
    spn(path_builder_line_to(pb, 175, 327));
    spn(path_builder_line_to(pb, 179, 332));
    spn(path_builder_line_to(pb, 183, 326));
    spn(path_builder_line_to(pb, 184, 332));
    spn(path_builder_line_to(pb, 189, 323));
    spn(path_builder_line_to(pb, 190, 328));
    spn(path_builder_line_to(pb, 194, 320));
    spn(path_builder_line_to(pb, 194, 325));
    spn(path_builder_line_to(pb, 199, 316));
    spn(path_builder_line_to(pb, 201, 320));
    spn(path_builder_line_to(pb, 204, 313));
    spn(path_builder_line_to(pb, 206, 316));
    spn(path_builder_line_to(pb, 208, 310));
    spn(path_builder_line_to(pb, 211, 305));
    spn(path_builder_line_to(pb, 219, 298));
    spn(path_builder_line_to(pb, 226, 288));
    spn(path_builder_line_to(pb, 229, 279));
    spn(path_builder_line_to(pb, 228, 266));
    spn(path_builder_line_to(pb, 224, 259));
    spn(path_builder_line_to(pb, 217, 253));
    spn(path_builder_line_to(pb, 212, 250));

    spn(path_builder_move_to(pb, 151, 205));
    spn(path_builder_line_to(pb, 151, 238));
    spn(path_builder_line_to(pb, 149, 252));
    spn(path_builder_line_to(pb, 141, 268));
    spn(path_builder_line_to(pb, 128, 282));
    spn(path_builder_line_to(pb, 121, 301));
    spn(path_builder_line_to(pb, 130, 300));
    spn(path_builder_line_to(pb, 126, 313));
    spn(path_builder_line_to(pb, 118, 324));
    spn(path_builder_line_to(pb, 116, 337));
    spn(path_builder_line_to(pb, 120, 346));
    spn(path_builder_line_to(pb, 133, 352));
    spn(path_builder_line_to(pb, 133, 340));
    spn(path_builder_line_to(pb, 137, 333));
    spn(path_builder_line_to(pb, 145, 329));
    spn(path_builder_line_to(pb, 156, 327));
    spn(path_builder_line_to(pb, 153, 319));
    spn(path_builder_line_to(pb, 153, 291));
    spn(path_builder_line_to(pb, 157, 271));
    spn(path_builder_line_to(pb, 170, 259));
    spn(path_builder_line_to(pb, 178, 277));
    spn(path_builder_line_to(pb, 193, 250));
    spn(path_builder_line_to(pb, 174, 216));
    spn(path_builder_line_to(pb, 151, 205));

    spn(path_builder_move_to(pb, 78, 127));
    spn(path_builder_line_to(pb, 90, 142));
    spn(path_builder_line_to(pb, 95, 155));
    spn(path_builder_line_to(pb, 108, 164));
    spn(path_builder_line_to(pb, 125, 167));
    spn(path_builder_line_to(pb, 139, 175));
    spn(path_builder_line_to(pb, 150, 206));
    spn(path_builder_line_to(pb, 152, 191));
    spn(path_builder_line_to(pb, 141, 140));
    spn(path_builder_line_to(pb, 121, 148));
    spn(path_builder_line_to(pb, 100, 136));
    spn(path_builder_line_to(pb, 78, 127));

    spn(path_builder_move_to(pb, 21, 58));
    spn(path_builder_line_to(pb, 35, 63));
    spn(path_builder_line_to(pb, 38, 68));
    spn(path_builder_line_to(pb, 32, 69));
    spn(path_builder_line_to(pb, 42, 74));
    spn(path_builder_line_to(pb, 40, 79));
    spn(path_builder_line_to(pb, 47, 80));
    spn(path_builder_line_to(pb, 54, 83));
    spn(path_builder_line_to(pb, 45, 94));
    spn(path_builder_line_to(pb, 34, 81));
    spn(path_builder_line_to(pb, 32, 73));
    spn(path_builder_line_to(pb, 24, 66));
    spn(path_builder_line_to(pb, 21, 58));

    spn(path_builder_move_to(pb, 71, 34));
    spn(path_builder_line_to(pb, 67, 34));
    spn(path_builder_line_to(pb, 66, 27));
    spn(path_builder_line_to(pb, 59, 24));
    spn(path_builder_line_to(pb, 54, 17));
    spn(path_builder_line_to(pb, 48, 17));
    spn(path_builder_line_to(pb, 39, 22));
    spn(path_builder_line_to(pb, 30, 26));
    spn(path_builder_line_to(pb, 28, 31));
    spn(path_builder_line_to(pb, 31, 39));
    spn(path_builder_line_to(pb, 38, 46));
    spn(path_builder_line_to(pb, 29, 45));
    spn(path_builder_line_to(pb, 36, 54));
    spn(path_builder_line_to(pb, 41, 61));
    spn(path_builder_line_to(pb, 41, 70));
    spn(path_builder_line_to(pb, 50, 69));
    spn(path_builder_line_to(pb, 54, 71));
    spn(path_builder_line_to(pb, 55, 58));
    spn(path_builder_line_to(pb, 67, 52));
    spn(path_builder_line_to(pb, 76, 43));
    spn(path_builder_line_to(pb, 76, 39));
    spn(path_builder_line_to(pb, 68, 44));
    spn(path_builder_line_to(pb, 71, 34));

    spn(path_builder_move_to(pb, 139, 74));
    spn(path_builder_line_to(pb, 141, 83));
    spn(path_builder_line_to(pb, 143, 89));
    spn(path_builder_line_to(pb, 144, 104));
    spn(path_builder_line_to(pb, 148, 104));
    spn(path_builder_line_to(pb, 155, 106));
    spn(path_builder_line_to(pb, 154, 86));
    spn(path_builder_line_to(pb, 157, 77));
    spn(path_builder_line_to(pb, 155, 72));
    spn(path_builder_line_to(pb, 150, 77));
    spn(path_builder_line_to(pb, 144, 77));
    spn(path_builder_line_to(pb, 139, 74));

    spn(path_builder_move_to(pb, 105, 44));
    spn(path_builder_line_to(pb, 102, 53));
    spn(path_builder_line_to(pb, 108, 58));
    spn(path_builder_line_to(pb, 111, 62));
    spn(path_builder_line_to(pb, 112, 55));
    spn(path_builder_line_to(pb, 105, 44));

    spn(path_builder_move_to(pb, 141, 48));
    spn(path_builder_line_to(pb, 141, 54));
    spn(path_builder_line_to(pb, 144, 58));
    spn(path_builder_line_to(pb, 139, 62));
    spn(path_builder_line_to(pb, 137, 66));
    spn(path_builder_line_to(pb, 136, 59));
    spn(path_builder_line_to(pb, 137, 52));
    spn(path_builder_line_to(pb, 141, 48));

    spn(path_builder_move_to(pb, 98, 135));
    spn(path_builder_line_to(pb, 104, 130));
    spn(path_builder_line_to(pb, 105, 134));
    spn(path_builder_line_to(pb, 108, 132));
    spn(path_builder_line_to(pb, 108, 135));
    spn(path_builder_line_to(pb, 112, 134));
    spn(path_builder_line_to(pb, 113, 137));
    spn(path_builder_line_to(pb, 116, 136));
    spn(path_builder_line_to(pb, 116, 139));
    spn(path_builder_line_to(pb, 119, 139));
    spn(path_builder_line_to(pb, 124, 141));
    spn(path_builder_line_to(pb, 128, 140));
    spn(path_builder_line_to(pb, 133, 138));
    spn(path_builder_line_to(pb, 140, 133));
    spn(path_builder_line_to(pb, 139, 140));
    spn(path_builder_line_to(pb, 126, 146));
    spn(path_builder_line_to(pb, 104, 144));
    spn(path_builder_line_to(pb, 98, 135));

    spn(path_builder_move_to(pb, 97, 116));
    spn(path_builder_line_to(pb, 103, 119));
    spn(path_builder_line_to(pb, 103, 116));
    spn(path_builder_line_to(pb, 111, 118));
    spn(path_builder_line_to(pb, 116, 117));
    spn(path_builder_line_to(pb, 122, 114));
    spn(path_builder_line_to(pb, 127, 107));
    spn(path_builder_line_to(pb, 135, 111));
    spn(path_builder_line_to(pb, 142, 107));
    spn(path_builder_line_to(pb, 141, 114));
    spn(path_builder_line_to(pb, 145, 118));
    spn(path_builder_line_to(pb, 149, 121));
    spn(path_builder_line_to(pb, 145, 125));
    spn(path_builder_line_to(pb, 140, 124));
    spn(path_builder_line_to(pb, 127, 121));
    spn(path_builder_line_to(pb, 113, 125));
    spn(path_builder_line_to(pb, 100, 124));
    spn(path_builder_line_to(pb, 97, 116));

    spn(path_builder_move_to(pb, 147, 33));
    spn(path_builder_line_to(pb, 152, 35));
    spn(path_builder_line_to(pb, 157, 34));
    spn(path_builder_line_to(pb, 153, 31));
    spn(path_builder_line_to(pb, 160, 31));
    spn(path_builder_line_to(pb, 156, 28));
    spn(path_builder_line_to(pb, 161, 28));
    spn(path_builder_line_to(pb, 159, 24));
    spn(path_builder_line_to(pb, 163, 25));
    spn(path_builder_line_to(pb, 163, 21));
    spn(path_builder_line_to(pb, 165, 22));
    spn(path_builder_line_to(pb, 170, 23));
    spn(path_builder_line_to(pb, 167, 17));
    spn(path_builder_line_to(pb, 172, 21));
    spn(path_builder_line_to(pb, 174, 18));
    spn(path_builder_line_to(pb, 175, 23));
    spn(path_builder_line_to(pb, 176, 22));
    spn(path_builder_line_to(pb, 177, 28));
    spn(path_builder_line_to(pb, 177, 33));
    spn(path_builder_line_to(pb, 174, 37));
    spn(path_builder_line_to(pb, 176, 39));
    spn(path_builder_line_to(pb, 174, 44));
    spn(path_builder_line_to(pb, 171, 49));
    spn(path_builder_line_to(pb, 168, 53));
    spn(path_builder_line_to(pb, 164, 57));
    spn(path_builder_line_to(pb, 159, 68));
    spn(path_builder_line_to(pb, 156, 70));
    spn(path_builder_line_to(pb, 154, 60));
    spn(path_builder_line_to(pb, 150, 51));
    spn(path_builder_line_to(pb, 146, 43));
    spn(path_builder_line_to(pb, 144, 35));
    spn(path_builder_line_to(pb, 147, 33));

    spn(path_builder_move_to(pb, 85, 72));
    spn(path_builder_line_to(pb, 89, 74));
    spn(path_builder_line_to(pb, 93, 75));
    spn(path_builder_line_to(pb, 100, 76));
    spn(path_builder_line_to(pb, 105, 75));
    spn(path_builder_line_to(pb, 102, 79));
    spn(path_builder_line_to(pb, 94, 79));
    spn(path_builder_line_to(pb, 88, 76));
    spn(path_builder_line_to(pb, 85, 72));

    spn(path_builder_move_to(pb, 86, 214));
    spn(path_builder_line_to(pb, 79, 221));
    spn(path_builder_line_to(pb, 76, 232));
    spn(path_builder_line_to(pb, 82, 225));
    spn(path_builder_line_to(pb, 78, 239));
    spn(path_builder_line_to(pb, 82, 234));
    spn(path_builder_line_to(pb, 78, 245));
    spn(path_builder_line_to(pb, 81, 243));
    spn(path_builder_line_to(pb, 79, 255));
    spn(path_builder_line_to(pb, 84, 250));
    spn(path_builder_line_to(pb, 84, 267));
    spn(path_builder_line_to(pb, 87, 254));
    spn(path_builder_line_to(pb, 90, 271));
    spn(path_builder_line_to(pb, 90, 257));
    spn(path_builder_line_to(pb, 95, 271));
    spn(path_builder_line_to(pb, 93, 256));
    spn(path_builder_line_to(pb, 95, 249));
    spn(path_builder_line_to(pb, 92, 252));
    spn(path_builder_line_to(pb, 93, 243));
    spn(path_builder_line_to(pb, 89, 253));
    spn(path_builder_line_to(pb, 89, 241));
    spn(path_builder_line_to(pb, 86, 250));
    spn(path_builder_line_to(pb, 87, 236));
    spn(path_builder_line_to(pb, 83, 245));
    spn(path_builder_line_to(pb, 87, 231));
    spn(path_builder_line_to(pb, 82, 231));
    spn(path_builder_line_to(pb, 90, 219));
    spn(path_builder_line_to(pb, 84, 221));
    spn(path_builder_line_to(pb, 86, 214));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 93, 68));
    spn(path_builder_line_to(pb, 96, 72));
    spn(path_builder_line_to(pb, 100, 73));
    spn(path_builder_line_to(pb, 106, 72));
    spn(path_builder_line_to(pb, 108, 66));
    spn(path_builder_line_to(pb, 105, 63));
    spn(path_builder_line_to(pb, 100, 62));
    spn(path_builder_line_to(pb, 93, 68));

    spn(path_builder_move_to(pb, 144, 64));
    spn(path_builder_line_to(pb, 142, 68));
    spn(path_builder_line_to(pb, 142, 73));
    spn(path_builder_line_to(pb, 146, 74));
    spn(path_builder_line_to(pb, 150, 73));
    spn(path_builder_line_to(pb, 154, 64));
    spn(path_builder_line_to(pb, 149, 62));
    spn(path_builder_line_to(pb, 144, 64));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 57, 91));
    spn(path_builder_line_to(pb, 42, 111));
    spn(path_builder_line_to(pb, 52, 105));
    spn(path_builder_line_to(pb, 41, 117));
    spn(path_builder_line_to(pb, 53, 112));
    spn(path_builder_line_to(pb, 46, 120));
    spn(path_builder_line_to(pb, 53, 116));
    spn(path_builder_line_to(pb, 50, 124));
    spn(path_builder_line_to(pb, 57, 119));
    spn(path_builder_line_to(pb, 55, 127));
    spn(path_builder_line_to(pb, 61, 122));
    spn(path_builder_line_to(pb, 60, 130));
    spn(path_builder_line_to(pb, 67, 126));
    spn(path_builder_line_to(pb, 66, 134));
    spn(path_builder_line_to(pb, 71, 129));
    spn(path_builder_line_to(pb, 72, 136));
    spn(path_builder_line_to(pb, 77, 130));
    spn(path_builder_line_to(pb, 76, 137));
    spn(path_builder_line_to(pb, 80, 133));
    spn(path_builder_line_to(pb, 82, 138));
    spn(path_builder_line_to(pb, 86, 135));
    spn(path_builder_line_to(pb, 96, 135));
    spn(path_builder_line_to(pb, 94, 129));
    spn(path_builder_line_to(pb, 86, 124));
    spn(path_builder_line_to(pb, 83, 117));
    spn(path_builder_line_to(pb, 77, 123));
    spn(path_builder_line_to(pb, 79, 117));
    spn(path_builder_line_to(pb, 73, 120));
    spn(path_builder_line_to(pb, 75, 112));
    spn(path_builder_line_to(pb, 68, 116));
    spn(path_builder_line_to(pb, 71, 111));
    spn(path_builder_line_to(pb, 65, 114));
    spn(path_builder_line_to(pb, 69, 107));
    spn(path_builder_line_to(pb, 63, 110));
    spn(path_builder_line_to(pb, 68, 102));
    spn(path_builder_line_to(pb, 61, 107));
    spn(path_builder_line_to(pb, 66, 98));
    spn(path_builder_line_to(pb, 61, 103));
    spn(path_builder_line_to(pb, 63, 97));
    spn(path_builder_line_to(pb, 57, 99));
    spn(path_builder_line_to(pb, 57, 91));

    spn(path_builder_move_to(pb, 83, 79));
    spn(path_builder_line_to(pb, 76, 79));
    spn(path_builder_line_to(pb, 67, 82));
    spn(path_builder_line_to(pb, 75, 83));
    spn(path_builder_line_to(pb, 65, 88));
    spn(path_builder_line_to(pb, 76, 87));
    spn(path_builder_line_to(pb, 65, 92));
    spn(path_builder_line_to(pb, 76, 91));
    spn(path_builder_line_to(pb, 68, 96));
    spn(path_builder_line_to(pb, 77, 95));
    spn(path_builder_line_to(pb, 70, 99));
    spn(path_builder_line_to(pb, 80, 98));
    spn(path_builder_line_to(pb, 72, 104));
    spn(path_builder_line_to(pb, 80, 102));
    spn(path_builder_line_to(pb, 76, 108));
    spn(path_builder_line_to(pb, 85, 103));
    spn(path_builder_line_to(pb, 92, 101));
    spn(path_builder_line_to(pb, 87, 98));
    spn(path_builder_line_to(pb, 93, 96));
    spn(path_builder_line_to(pb, 86, 94));
    spn(path_builder_line_to(pb, 91, 93));
    spn(path_builder_line_to(pb, 85, 91));
    spn(path_builder_line_to(pb, 93, 89));
    spn(path_builder_line_to(pb, 99, 89));
    spn(path_builder_line_to(pb, 105, 93));
    spn(path_builder_line_to(pb, 107, 85));
    spn(path_builder_line_to(pb, 102, 82));
    spn(path_builder_line_to(pb, 92, 80));
    spn(path_builder_line_to(pb, 83, 79));

    spn(path_builder_move_to(pb, 109, 77));
    spn(path_builder_line_to(pb, 111, 83));
    spn(path_builder_line_to(pb, 109, 89));
    spn(path_builder_line_to(pb, 113, 94));
    spn(path_builder_line_to(pb, 117, 90));
    spn(path_builder_line_to(pb, 117, 81));
    spn(path_builder_line_to(pb, 114, 78));
    spn(path_builder_line_to(pb, 109, 77));

    spn(path_builder_move_to(pb, 122, 128));
    spn(path_builder_line_to(pb, 127, 126));
    spn(path_builder_line_to(pb, 134, 127));
    spn(path_builder_line_to(pb, 136, 129));
    spn(path_builder_line_to(pb, 134, 130));
    spn(path_builder_line_to(pb, 130, 128));
    spn(path_builder_line_to(pb, 124, 129));
    spn(path_builder_line_to(pb, 122, 128));

    spn(path_builder_move_to(pb, 78, 27));
    spn(path_builder_line_to(pb, 82, 32));
    spn(path_builder_line_to(pb, 80, 33));
    spn(path_builder_line_to(pb, 82, 36));
    spn(path_builder_line_to(pb, 78, 37));
    spn(path_builder_line_to(pb, 82, 40));
    spn(path_builder_line_to(pb, 78, 42));
    spn(path_builder_line_to(pb, 81, 46));
    spn(path_builder_line_to(pb, 76, 47));
    spn(path_builder_line_to(pb, 78, 49));
    spn(path_builder_line_to(pb, 74, 50));
    spn(path_builder_line_to(pb, 82, 52));
    spn(path_builder_line_to(pb, 87, 50));
    spn(path_builder_line_to(pb, 83, 48));
    spn(path_builder_line_to(pb, 91, 46));
    spn(path_builder_line_to(pb, 86, 45));
    spn(path_builder_line_to(pb, 91, 42));
    spn(path_builder_line_to(pb, 88, 40));
    spn(path_builder_line_to(pb, 92, 37));
    spn(path_builder_line_to(pb, 86, 34));
    spn(path_builder_line_to(pb, 90, 31));
    spn(path_builder_line_to(pb, 86, 29));
    spn(path_builder_line_to(pb, 89, 26));
    spn(path_builder_line_to(pb, 78, 27));

    spn(path_builder_move_to(pb, 82, 17));
    spn(path_builder_line_to(pb, 92, 20));
    spn(path_builder_line_to(pb, 79, 21));
    spn(path_builder_line_to(pb, 90, 25));
    spn(path_builder_line_to(pb, 81, 25));
    spn(path_builder_line_to(pb, 94, 28));
    spn(path_builder_line_to(pb, 93, 26));
    spn(path_builder_line_to(pb, 101, 30));
    spn(path_builder_line_to(pb, 101, 26));
    spn(path_builder_line_to(pb, 107, 33));
    spn(path_builder_line_to(pb, 108, 28));
    spn(path_builder_line_to(pb, 111, 40));
    spn(path_builder_line_to(pb, 113, 34));
    spn(path_builder_line_to(pb, 115, 45));
    spn(path_builder_line_to(pb, 117, 39));
    spn(path_builder_line_to(pb, 119, 54));
    spn(path_builder_line_to(pb, 121, 46));
    spn(path_builder_line_to(pb, 124, 58));
    spn(path_builder_line_to(pb, 126, 47));
    spn(path_builder_line_to(pb, 129, 59));
    spn(path_builder_line_to(pb, 130, 49));
    spn(path_builder_line_to(pb, 134, 58));
    spn(path_builder_line_to(pb, 133, 44));
    spn(path_builder_line_to(pb, 137, 48));
    spn(path_builder_line_to(pb, 133, 37));
    spn(path_builder_line_to(pb, 137, 40));
    spn(path_builder_line_to(pb, 133, 32));
    spn(path_builder_line_to(pb, 126, 20));
    spn(path_builder_line_to(pb, 135, 26));
    spn(path_builder_line_to(pb, 132, 19));
    spn(path_builder_line_to(pb, 138, 23));
    spn(path_builder_line_to(pb, 135, 17));
    spn(path_builder_line_to(pb, 142, 18));
    spn(path_builder_line_to(pb, 132, 11));
    spn(path_builder_line_to(pb, 116, 6));
    spn(path_builder_line_to(pb, 94, 6));
    spn(path_builder_line_to(pb, 78, 11));
    spn(path_builder_line_to(pb, 92, 12));
    spn(path_builder_line_to(pb, 80, 14));
    spn(path_builder_line_to(pb, 90, 16));
    spn(path_builder_line_to(pb, 82, 17));

    spn(path_builder_move_to(pb, 142, 234));
    spn(path_builder_line_to(pb, 132, 227));
    spn(path_builder_line_to(pb, 124, 223));
    spn(path_builder_line_to(pb, 115, 220));
    spn(path_builder_line_to(pb, 110, 225));
    spn(path_builder_line_to(pb, 118, 224));
    spn(path_builder_line_to(pb, 127, 229));
    spn(path_builder_line_to(pb, 135, 236));
    spn(path_builder_line_to(pb, 122, 234));
    spn(path_builder_line_to(pb, 115, 237));
    spn(path_builder_line_to(pb, 113, 242));
    spn(path_builder_line_to(pb, 121, 238));
    spn(path_builder_line_to(pb, 139, 243));
    spn(path_builder_line_to(pb, 121, 245));
    spn(path_builder_line_to(pb, 111, 254));
    spn(path_builder_line_to(pb, 95, 254));
    spn(path_builder_line_to(pb, 102, 244));
    spn(path_builder_line_to(pb, 104, 235));
    spn(path_builder_line_to(pb, 110, 229));
    spn(path_builder_line_to(pb, 100, 231));
    spn(path_builder_line_to(pb, 104, 224));
    spn(path_builder_line_to(pb, 113, 216));
    spn(path_builder_line_to(pb, 122, 215));
    spn(path_builder_line_to(pb, 132, 217));
    spn(path_builder_line_to(pb, 141, 224));
    spn(path_builder_line_to(pb, 145, 230));
    spn(path_builder_line_to(pb, 149, 240));
    spn(path_builder_line_to(pb, 142, 234));

    spn(path_builder_move_to(pb, 115, 252));
    spn(path_builder_line_to(pb, 125, 248));
    spn(path_builder_line_to(pb, 137, 249));
    spn(path_builder_line_to(pb, 143, 258));
    spn(path_builder_line_to(pb, 134, 255));
    spn(path_builder_line_to(pb, 125, 254));
    spn(path_builder_line_to(pb, 115, 252));

    spn(path_builder_move_to(pb, 114, 212));
    spn(path_builder_line_to(pb, 130, 213));
    spn(path_builder_line_to(pb, 140, 219));
    spn(path_builder_line_to(pb, 147, 225));
    spn(path_builder_line_to(pb, 144, 214));
    spn(path_builder_line_to(pb, 137, 209));
    spn(path_builder_line_to(pb, 128, 207));
    spn(path_builder_line_to(pb, 114, 212));

    spn(path_builder_move_to(pb, 102, 263));
    spn(path_builder_line_to(pb, 108, 258));
    spn(path_builder_line_to(pb, 117, 257));
    spn(path_builder_line_to(pb, 131, 258));
    spn(path_builder_line_to(pb, 116, 260));
    spn(path_builder_line_to(pb, 109, 265));
    spn(path_builder_line_to(pb, 102, 263));

    spn(path_builder_move_to(pb, 51, 241));
    spn(path_builder_line_to(pb, 35, 224));
    spn(path_builder_line_to(pb, 40, 238));
    spn(path_builder_line_to(pb, 23, 224));
    spn(path_builder_line_to(pb, 31, 242));
    spn(path_builder_line_to(pb, 19, 239));
    spn(path_builder_line_to(pb, 28, 247));
    spn(path_builder_line_to(pb, 17, 246));
    spn(path_builder_line_to(pb, 25, 250));
    spn(path_builder_line_to(pb, 37, 254));
    spn(path_builder_line_to(pb, 39, 263));
    spn(path_builder_line_to(pb, 44, 271));
    spn(path_builder_line_to(pb, 47, 294));
    spn(path_builder_line_to(pb, 48, 317));
    spn(path_builder_line_to(pb, 51, 328));
    spn(path_builder_line_to(pb, 60, 351));
    spn(path_builder_line_to(pb, 60, 323));
    spn(path_builder_line_to(pb, 53, 262));
    spn(path_builder_line_to(pb, 47, 246));
    spn(path_builder_line_to(pb, 51, 241));

    spn(path_builder_move_to(pb, 2, 364));
    spn(path_builder_line_to(pb, 9, 367));
    spn(path_builder_line_to(pb, 14, 366));
    spn(path_builder_line_to(pb, 18, 355));
    spn(path_builder_line_to(pb, 20, 364));
    spn(path_builder_line_to(pb, 26, 366));
    spn(path_builder_line_to(pb, 31, 357));
    spn(path_builder_line_to(pb, 35, 364));
    spn(path_builder_line_to(pb, 39, 364));
    spn(path_builder_line_to(pb, 42, 357));
    spn(path_builder_line_to(pb, 47, 363));
    spn(path_builder_line_to(pb, 53, 360));
    spn(path_builder_line_to(pb, 59, 357));
    spn(path_builder_line_to(pb, 54, 369));
    spn(path_builder_line_to(pb, 7, 373));
    spn(path_builder_line_to(pb, 2, 364));

    spn(path_builder_move_to(pb, 7, 349));
    spn(path_builder_line_to(pb, 19, 345));
    spn(path_builder_line_to(pb, 25, 339));
    spn(path_builder_line_to(pb, 18, 341));
    spn(path_builder_line_to(pb, 23, 333));
    spn(path_builder_line_to(pb, 28, 326));
    spn(path_builder_line_to(pb, 23, 326));
    spn(path_builder_line_to(pb, 27, 320));
    spn(path_builder_line_to(pb, 23, 316));
    spn(path_builder_line_to(pb, 25, 311));
    spn(path_builder_line_to(pb, 20, 298));
    spn(path_builder_line_to(pb, 15, 277));
    spn(path_builder_line_to(pb, 12, 264));
    spn(path_builder_line_to(pb, 9, 249));
    spn(path_builder_line_to(pb, 10, 223));
    spn(path_builder_line_to(pb, 3, 248));
    spn(path_builder_line_to(pb, 5, 261));
    spn(path_builder_line_to(pb, 15, 307));
    spn(path_builder_line_to(pb, 17, 326));
    spn(path_builder_line_to(pb, 11, 343));
    spn(path_builder_line_to(pb, 7, 349));

    spn(path_builder_move_to(pb, 11, 226));
    spn(path_builder_line_to(pb, 15, 231));
    spn(path_builder_line_to(pb, 25, 236));
    spn(path_builder_line_to(pb, 18, 227));
    spn(path_builder_line_to(pb, 11, 226));

    spn(path_builder_move_to(pb, 13, 214));
    spn(path_builder_line_to(pb, 19, 217));
    spn(path_builder_line_to(pb, 32, 227));
    spn(path_builder_line_to(pb, 23, 214));
    spn(path_builder_line_to(pb, 16, 208));
    spn(path_builder_line_to(pb, 15, 190));
    spn(path_builder_line_to(pb, 24, 148));
    spn(path_builder_line_to(pb, 31, 121));
    spn(path_builder_line_to(pb, 24, 137));
    spn(path_builder_line_to(pb, 14, 170));
    spn(path_builder_line_to(pb, 8, 189));
    spn(path_builder_line_to(pb, 13, 214));

    spn(path_builder_move_to(pb, 202, 254));
    spn(path_builder_line_to(pb, 195, 258));
    spn(path_builder_line_to(pb, 199, 260));
    spn(path_builder_line_to(pb, 193, 263));
    spn(path_builder_line_to(pb, 197, 263));
    spn(path_builder_line_to(pb, 190, 268));
    spn(path_builder_line_to(pb, 196, 268));
    spn(path_builder_line_to(pb, 191, 273));
    spn(path_builder_line_to(pb, 188, 282));
    spn(path_builder_line_to(pb, 200, 272));
    spn(path_builder_line_to(pb, 194, 272));
    spn(path_builder_line_to(pb, 201, 266));
    spn(path_builder_line_to(pb, 197, 265));
    spn(path_builder_line_to(pb, 204, 262));
    spn(path_builder_line_to(pb, 200, 258));
    spn(path_builder_line_to(pb, 204, 256));
    spn(path_builder_line_to(pb, 202, 254));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 151, 213));
    spn(path_builder_line_to(pb, 165, 212));
    spn(path_builder_line_to(pb, 179, 225));
    spn(path_builder_line_to(pb, 189, 246));
    spn(path_builder_line_to(pb, 187, 262));
    spn(path_builder_line_to(pb, 179, 275));
    spn(path_builder_line_to(pb, 176, 263));
    spn(path_builder_line_to(pb, 177, 247));
    spn(path_builder_line_to(pb, 171, 233));
    spn(path_builder_line_to(pb, 163, 230));
    spn(path_builder_line_to(pb, 165, 251));
    spn(path_builder_line_to(pb, 157, 264));
    spn(path_builder_line_to(pb, 146, 298));
    spn(path_builder_line_to(pb, 145, 321));
    spn(path_builder_line_to(pb, 133, 326));
    spn(path_builder_line_to(pb, 143, 285));
    spn(path_builder_line_to(pb, 154, 260));
    spn(path_builder_line_to(pb, 153, 240));
    spn(path_builder_line_to(pb, 151, 213));

    spn(path_builder_move_to(pb, 91, 132));
    spn(path_builder_line_to(pb, 95, 145));
    spn(path_builder_line_to(pb, 97, 154));
    spn(path_builder_line_to(pb, 104, 148));
    spn(path_builder_line_to(pb, 107, 155));
    spn(path_builder_line_to(pb, 109, 150));
    spn(path_builder_line_to(pb, 111, 158));
    spn(path_builder_line_to(pb, 115, 152));
    spn(path_builder_line_to(pb, 118, 159));
    spn(path_builder_line_to(pb, 120, 153));
    spn(path_builder_line_to(pb, 125, 161));
    spn(path_builder_line_to(pb, 126, 155));
    spn(path_builder_line_to(pb, 133, 164));
    spn(path_builder_line_to(pb, 132, 154));
    spn(path_builder_line_to(pb, 137, 163));
    spn(path_builder_line_to(pb, 137, 152));
    spn(path_builder_line_to(pb, 142, 163));
    spn(path_builder_line_to(pb, 147, 186));
    spn(path_builder_line_to(pb, 152, 192));
    spn(path_builder_line_to(pb, 148, 167));
    spn(path_builder_line_to(pb, 141, 143));
    spn(path_builder_line_to(pb, 124, 145));
    spn(path_builder_line_to(pb, 105, 143));
    spn(path_builder_line_to(pb, 91, 132));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 31, 57));
    spn(path_builder_line_to(pb, 23, 52));
    spn(path_builder_line_to(pb, 26, 51));
    spn(path_builder_line_to(pb, 20, 44));
    spn(path_builder_line_to(pb, 23, 42));
    spn(path_builder_line_to(pb, 21, 36));
    spn(path_builder_line_to(pb, 22, 29));
    spn(path_builder_line_to(pb, 25, 23));
    spn(path_builder_line_to(pb, 24, 32));
    spn(path_builder_line_to(pb, 30, 43));
    spn(path_builder_line_to(pb, 26, 41));
    spn(path_builder_line_to(pb, 30, 50));
    spn(path_builder_line_to(pb, 26, 48));
    spn(path_builder_line_to(pb, 31, 57));

    spn(path_builder_move_to(pb, 147, 21));
    spn(path_builder_line_to(pb, 149, 28));
    spn(path_builder_line_to(pb, 155, 21));
    spn(path_builder_line_to(pb, 161, 16));
    spn(path_builder_line_to(pb, 167, 14));
    spn(path_builder_line_to(pb, 175, 15));
    spn(path_builder_line_to(pb, 173, 11));
    spn(path_builder_line_to(pb, 161, 9));
    spn(path_builder_line_to(pb, 147, 21));

    spn(path_builder_move_to(pb, 181, 39));
    spn(path_builder_line_to(pb, 175, 51));
    spn(path_builder_line_to(pb, 169, 57));
    spn(path_builder_line_to(pb, 171, 65));
    spn(path_builder_line_to(pb, 165, 68));
    spn(path_builder_line_to(pb, 165, 75));
    spn(path_builder_line_to(pb, 160, 76));
    spn(path_builder_line_to(pb, 162, 91));
    spn(path_builder_line_to(pb, 171, 71));
    spn(path_builder_line_to(pb, 180, 51));
    spn(path_builder_line_to(pb, 181, 39));

    spn(path_builder_move_to(pb, 132, 346));
    spn(path_builder_line_to(pb, 139, 348));
    spn(path_builder_line_to(pb, 141, 346));
    spn(path_builder_line_to(pb, 142, 341));
    spn(path_builder_line_to(pb, 147, 342));
    spn(path_builder_line_to(pb, 143, 355));
    spn(path_builder_line_to(pb, 133, 350));
    spn(path_builder_line_to(pb, 132, 346));

    spn(path_builder_move_to(pb, 146, 355));
    spn(path_builder_line_to(pb, 151, 352));
    spn(path_builder_line_to(pb, 155, 348));
    spn(path_builder_line_to(pb, 157, 343));
    spn(path_builder_line_to(pb, 160, 349));
    spn(path_builder_line_to(pb, 151, 356));
    spn(path_builder_line_to(pb, 147, 357));
    spn(path_builder_line_to(pb, 146, 355));

    spn(path_builder_move_to(pb, 99, 266));
    spn(path_builder_line_to(pb, 100, 281));
    spn(path_builder_line_to(pb, 94, 305));
    spn(path_builder_line_to(pb, 86, 322));
    spn(path_builder_line_to(pb, 78, 332));
    spn(path_builder_line_to(pb, 72, 346));
    spn(path_builder_line_to(pb, 73, 331));
    spn(path_builder_line_to(pb, 91, 291));
    spn(path_builder_line_to(pb, 99, 266));

    spn(path_builder_move_to(pb, 20, 347));
    spn(path_builder_line_to(pb, 32, 342));
    spn(path_builder_line_to(pb, 45, 340));
    spn(path_builder_line_to(pb, 54, 345));
    spn(path_builder_line_to(pb, 45, 350));
    spn(path_builder_line_to(pb, 42, 353));
    spn(path_builder_line_to(pb, 38, 350));
    spn(path_builder_line_to(pb, 31, 353));
    spn(path_builder_line_to(pb, 29, 356));
    spn(path_builder_line_to(pb, 23, 350));
    spn(path_builder_line_to(pb, 19, 353));
    spn(path_builder_line_to(pb, 15, 349));
    spn(path_builder_line_to(pb, 20, 347));

    spn(path_builder_move_to(pb, 78, 344));
    spn(path_builder_line_to(pb, 86, 344));
    spn(path_builder_line_to(pb, 92, 349));
    spn(path_builder_line_to(pb, 88, 358));
    spn(path_builder_line_to(pb, 84, 352));
    spn(path_builder_line_to(pb, 78, 344));

    spn(path_builder_move_to(pb, 93, 347));
    spn(path_builder_line_to(pb, 104, 344));
    spn(path_builder_line_to(pb, 117, 345));
    spn(path_builder_line_to(pb, 124, 354));
    spn(path_builder_line_to(pb, 121, 357));
    spn(path_builder_line_to(pb, 116, 351));
    spn(path_builder_line_to(pb, 112, 351));
    spn(path_builder_line_to(pb, 108, 355));
    spn(path_builder_line_to(pb, 102, 351));
    spn(path_builder_line_to(pb, 93, 347));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 105, 12));
    spn(path_builder_line_to(pb, 111, 18));
    spn(path_builder_line_to(pb, 113, 24));
    spn(path_builder_line_to(pb, 113, 29));
    spn(path_builder_line_to(pb, 119, 34));
    spn(path_builder_line_to(pb, 116, 23));
    spn(path_builder_line_to(pb, 112, 16));
    spn(path_builder_line_to(pb, 105, 12));

    spn(path_builder_move_to(pb, 122, 27));
    spn(path_builder_line_to(pb, 125, 34));
    spn(path_builder_line_to(pb, 127, 43));
    spn(path_builder_line_to(pb, 128, 34));
    spn(path_builder_line_to(pb, 125, 29));
    spn(path_builder_line_to(pb, 122, 27));

    spn(path_builder_move_to(pb, 115, 13));
    spn(path_builder_line_to(pb, 122, 19));
    spn(path_builder_line_to(pb, 122, 15));
    spn(path_builder_line_to(pb, 113, 10));
    spn(path_builder_line_to(pb, 115, 13));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 116, 172));
    spn(path_builder_line_to(pb, 107, 182));
    spn(path_builder_line_to(pb, 98, 193));
    spn(path_builder_line_to(pb, 98, 183));
    spn(path_builder_line_to(pb, 90, 199));
    spn(path_builder_line_to(pb, 89, 189));
    spn(path_builder_line_to(pb, 84, 207));
    spn(path_builder_line_to(pb, 88, 206));
    spn(path_builder_line_to(pb, 87, 215));
    spn(path_builder_line_to(pb, 95, 206));
    spn(path_builder_line_to(pb, 93, 219));
    spn(path_builder_line_to(pb, 91, 230));
    spn(path_builder_line_to(pb, 98, 216));
    spn(path_builder_line_to(pb, 97, 226));
    spn(path_builder_line_to(pb, 104, 214));
    spn(path_builder_line_to(pb, 112, 209));
    spn(path_builder_line_to(pb, 104, 208));
    spn(path_builder_line_to(pb, 113, 202));
    spn(path_builder_line_to(pb, 126, 200));
    spn(path_builder_line_to(pb, 139, 207));
    spn(path_builder_line_to(pb, 132, 198));
    spn(path_builder_line_to(pb, 142, 203));
    spn(path_builder_line_to(pb, 134, 192));
    spn(path_builder_line_to(pb, 142, 195));
    spn(path_builder_line_to(pb, 134, 187));
    spn(path_builder_line_to(pb, 140, 185));
    spn(path_builder_line_to(pb, 130, 181));
    spn(path_builder_line_to(pb, 136, 177));
    spn(path_builder_line_to(pb, 126, 177));
    spn(path_builder_line_to(pb, 125, 171));
    spn(path_builder_line_to(pb, 116, 180));
    spn(path_builder_line_to(pb, 116, 172));

    spn(path_builder_move_to(pb, 74, 220));
    spn(path_builder_line_to(pb, 67, 230));
    spn(path_builder_line_to(pb, 67, 221));
    spn(path_builder_line_to(pb, 59, 235));
    spn(path_builder_line_to(pb, 63, 233));
    spn(path_builder_line_to(pb, 60, 248));
    spn(path_builder_line_to(pb, 70, 232));
    spn(path_builder_line_to(pb, 65, 249));
    spn(path_builder_line_to(pb, 71, 243));
    spn(path_builder_line_to(pb, 67, 256));
    spn(path_builder_line_to(pb, 73, 250));
    spn(path_builder_line_to(pb, 69, 262));
    spn(path_builder_line_to(pb, 73, 259));
    spn(path_builder_line_to(pb, 71, 267));
    spn(path_builder_line_to(pb, 76, 262));
    spn(path_builder_line_to(pb, 72, 271));
    spn(path_builder_line_to(pb, 78, 270));
    spn(path_builder_line_to(pb, 76, 275));
    spn(path_builder_line_to(pb, 82, 274));
    spn(path_builder_line_to(pb, 78, 290));
    spn(path_builder_line_to(pb, 86, 279));
    spn(path_builder_line_to(pb, 86, 289));
    spn(path_builder_line_to(pb, 92, 274));
    spn(path_builder_line_to(pb, 88, 275));
    spn(path_builder_line_to(pb, 87, 264));
    spn(path_builder_line_to(pb, 82, 270));
    spn(path_builder_line_to(pb, 82, 258));
    spn(path_builder_line_to(pb, 77, 257));
    spn(path_builder_line_to(pb, 78, 247));
    spn(path_builder_line_to(pb, 73, 246));
    spn(path_builder_line_to(pb, 77, 233));
    spn(path_builder_line_to(pb, 72, 236));
    spn(path_builder_line_to(pb, 74, 220));

    spn(path_builder_move_to(pb, 133, 230));
    spn(path_builder_line_to(pb, 147, 242));
    spn(path_builder_line_to(pb, 148, 250));
    spn(path_builder_line_to(pb, 145, 254));
    spn(path_builder_line_to(pb, 138, 247));
    spn(path_builder_line_to(pb, 129, 246));
    spn(path_builder_line_to(pb, 142, 245));
    spn(path_builder_line_to(pb, 138, 241));
    spn(path_builder_line_to(pb, 128, 237));
    spn(path_builder_line_to(pb, 137, 238));
    spn(path_builder_line_to(pb, 133, 230));

    spn(path_builder_move_to(pb, 133, 261));
    spn(path_builder_line_to(pb, 125, 261));
    spn(path_builder_line_to(pb, 116, 263));
    spn(path_builder_line_to(pb, 111, 267));
    spn(path_builder_line_to(pb, 125, 265));
    spn(path_builder_line_to(pb, 133, 261));

    spn(path_builder_move_to(pb, 121, 271));
    spn(path_builder_line_to(pb, 109, 273));
    spn(path_builder_line_to(pb, 103, 279));
    spn(path_builder_line_to(pb, 99, 305));
    spn(path_builder_line_to(pb, 92, 316));
    spn(path_builder_line_to(pb, 85, 327));
    spn(path_builder_line_to(pb, 83, 335));
    spn(path_builder_line_to(pb, 89, 340));
    spn(path_builder_line_to(pb, 97, 341));
    spn(path_builder_line_to(pb, 94, 336));
    spn(path_builder_line_to(pb, 101, 336));
    spn(path_builder_line_to(pb, 96, 331));
    spn(path_builder_line_to(pb, 103, 330));
    spn(path_builder_line_to(pb, 97, 327));
    spn(path_builder_line_to(pb, 108, 325));
    spn(path_builder_line_to(pb, 99, 322));
    spn(path_builder_line_to(pb, 109, 321));
    spn(path_builder_line_to(pb, 100, 318));
    spn(path_builder_line_to(pb, 110, 317));
    spn(path_builder_line_to(pb, 105, 314));
    spn(path_builder_line_to(pb, 110, 312));
    spn(path_builder_line_to(pb, 107, 310));
    spn(path_builder_line_to(pb, 113, 308));
    spn(path_builder_line_to(pb, 105, 306));
    spn(path_builder_line_to(pb, 114, 303));
    spn(path_builder_line_to(pb, 105, 301));
    spn(path_builder_line_to(pb, 115, 298));
    spn(path_builder_line_to(pb, 107, 295));
    spn(path_builder_line_to(pb, 115, 294));
    spn(path_builder_line_to(pb, 108, 293));
    spn(path_builder_line_to(pb, 117, 291));
    spn(path_builder_line_to(pb, 109, 289));
    spn(path_builder_line_to(pb, 117, 286));
    spn(path_builder_line_to(pb, 109, 286));
    spn(path_builder_line_to(pb, 118, 283));
    spn(path_builder_line_to(pb, 112, 281));
    spn(path_builder_line_to(pb, 118, 279));
    spn(path_builder_line_to(pb, 114, 278));
    spn(path_builder_line_to(pb, 119, 276));
    spn(path_builder_line_to(pb, 115, 274));
    spn(path_builder_line_to(pb, 121, 271));

    spn(path_builder_move_to(pb, 79, 364));
    spn(path_builder_line_to(pb, 74, 359));
    spn(path_builder_line_to(pb, 74, 353));
    spn(path_builder_line_to(pb, 76, 347));
    spn(path_builder_line_to(pb, 80, 351));
    spn(path_builder_line_to(pb, 83, 356));
    spn(path_builder_line_to(pb, 82, 360));
    spn(path_builder_line_to(pb, 79, 364));

    spn(path_builder_move_to(pb, 91, 363));
    spn(path_builder_line_to(pb, 93, 356));
    spn(path_builder_line_to(pb, 97, 353));
    spn(path_builder_line_to(pb, 103, 355));
    spn(path_builder_line_to(pb, 105, 360));
    spn(path_builder_line_to(pb, 103, 366));
    spn(path_builder_line_to(pb, 99, 371));
    spn(path_builder_line_to(pb, 94, 368));
    spn(path_builder_line_to(pb, 91, 363));

    spn(path_builder_move_to(pb, 110, 355));
    spn(path_builder_line_to(pb, 114, 353));
    spn(path_builder_line_to(pb, 118, 357));
    spn(path_builder_line_to(pb, 117, 363));
    spn(path_builder_line_to(pb, 113, 369));
    spn(path_builder_line_to(pb, 111, 362));
    spn(path_builder_line_to(pb, 110, 355));

    spn(path_builder_move_to(pb, 126, 354));
    spn(path_builder_line_to(pb, 123, 358));
    spn(path_builder_line_to(pb, 124, 367));
    spn(path_builder_line_to(pb, 126, 369));
    spn(path_builder_line_to(pb, 129, 361));
    spn(path_builder_line_to(pb, 129, 357));
    spn(path_builder_line_to(pb, 126, 354));

    spn(path_builder_move_to(pb, 30, 154));
    spn(path_builder_line_to(pb, 24, 166));
    spn(path_builder_line_to(pb, 20, 182));
    spn(path_builder_line_to(pb, 23, 194));
    spn(path_builder_line_to(pb, 29, 208));
    spn(path_builder_line_to(pb, 37, 218));
    spn(path_builder_line_to(pb, 41, 210));
    spn(path_builder_line_to(pb, 41, 223));
    spn(path_builder_line_to(pb, 46, 214));
    spn(path_builder_line_to(pb, 46, 227));
    spn(path_builder_line_to(pb, 52, 216));
    spn(path_builder_line_to(pb, 52, 227));
    spn(path_builder_line_to(pb, 61, 216));
    spn(path_builder_line_to(pb, 59, 225));
    spn(path_builder_line_to(pb, 68, 213));
    spn(path_builder_line_to(pb, 73, 219));
    spn(path_builder_line_to(pb, 70, 207));
    spn(path_builder_line_to(pb, 77, 212));
    spn(path_builder_line_to(pb, 69, 200));
    spn(path_builder_line_to(pb, 77, 202));
    spn(path_builder_line_to(pb, 70, 194));
    spn(path_builder_line_to(pb, 78, 197));
    spn(path_builder_line_to(pb, 68, 187));
    spn(path_builder_line_to(pb, 76, 182));
    spn(path_builder_line_to(pb, 64, 182));
    spn(path_builder_line_to(pb, 58, 175));
    spn(path_builder_line_to(pb, 58, 185));
    spn(path_builder_line_to(pb, 53, 177));
    spn(path_builder_line_to(pb, 50, 186));
    spn(path_builder_line_to(pb, 46, 171));
    spn(path_builder_line_to(pb, 44, 182));
    spn(path_builder_line_to(pb, 39, 167));
    spn(path_builder_line_to(pb, 36, 172));
    spn(path_builder_line_to(pb, 36, 162));
    spn(path_builder_line_to(pb, 30, 166));
    spn(path_builder_line_to(pb, 30, 154));

    spn(path_builder_move_to(pb, 44, 130));
    spn(path_builder_line_to(pb, 41, 137));
    spn(path_builder_line_to(pb, 45, 136));
    spn(path_builder_line_to(pb, 43, 150));
    spn(path_builder_line_to(pb, 48, 142));
    spn(path_builder_line_to(pb, 48, 157));
    spn(path_builder_line_to(pb, 53, 150));
    spn(path_builder_line_to(pb, 52, 164));
    spn(path_builder_line_to(pb, 60, 156));
    spn(path_builder_line_to(pb, 61, 169));
    spn(path_builder_line_to(pb, 64, 165));
    spn(path_builder_line_to(pb, 66, 175));
    spn(path_builder_line_to(pb, 70, 167));
    spn(path_builder_line_to(pb, 74, 176));
    spn(path_builder_line_to(pb, 77, 168));
    spn(path_builder_line_to(pb, 80, 183));
    spn(path_builder_line_to(pb, 85, 172));
    spn(path_builder_line_to(pb, 90, 182));
    spn(path_builder_line_to(pb, 93, 174));
    spn(path_builder_line_to(pb, 98, 181));
    spn(path_builder_line_to(pb, 99, 173));
    spn(path_builder_line_to(pb, 104, 175));
    spn(path_builder_line_to(pb, 105, 169));
    spn(path_builder_line_to(pb, 114, 168));
    spn(path_builder_line_to(pb, 102, 163));
    spn(path_builder_line_to(pb, 95, 157));
    spn(path_builder_line_to(pb, 94, 166));
    spn(path_builder_line_to(pb, 90, 154));
    spn(path_builder_line_to(pb, 87, 162));
    spn(path_builder_line_to(pb, 82, 149));
    spn(path_builder_line_to(pb, 75, 159));
    spn(path_builder_line_to(pb, 72, 148));
    spn(path_builder_line_to(pb, 68, 155));
    spn(path_builder_line_to(pb, 67, 143));
    spn(path_builder_line_to(pb, 62, 148));
    spn(path_builder_line_to(pb, 62, 138));
    spn(path_builder_line_to(pb, 58, 145));
    spn(path_builder_line_to(pb, 56, 133));
    spn(path_builder_line_to(pb, 52, 142));
    spn(path_builder_line_to(pb, 52, 128));
    spn(path_builder_line_to(pb, 49, 134));
    spn(path_builder_line_to(pb, 47, 125));
    spn(path_builder_line_to(pb, 44, 130));

    spn(path_builder_move_to(pb, 13, 216));
    spn(path_builder_line_to(pb, 19, 219));
    spn(path_builder_line_to(pb, 36, 231));
    spn(path_builder_line_to(pb, 22, 223));
    spn(path_builder_line_to(pb, 16, 222));
    spn(path_builder_line_to(pb, 22, 227));
    spn(path_builder_line_to(pb, 12, 224));
    spn(path_builder_line_to(pb, 13, 220));
    spn(path_builder_line_to(pb, 16, 220));
    spn(path_builder_line_to(pb, 13, 216));

    spn(path_builder_move_to(pb, 10, 231));
    spn(path_builder_line_to(pb, 14, 236));
    spn(path_builder_line_to(pb, 25, 239));
    spn(path_builder_line_to(pb, 27, 237));
    spn(path_builder_line_to(pb, 19, 234));
    spn(path_builder_line_to(pb, 10, 231));

    spn(path_builder_move_to(pb, 9, 245));
    spn(path_builder_line_to(pb, 14, 242));
    spn(path_builder_line_to(pb, 25, 245));
    spn(path_builder_line_to(pb, 13, 245));
    spn(path_builder_line_to(pb, 9, 245));

    spn(path_builder_move_to(pb, 33, 255));
    spn(path_builder_line_to(pb, 26, 253));
    spn(path_builder_line_to(pb, 18, 254));
    spn(path_builder_line_to(pb, 25, 256));
    spn(path_builder_line_to(pb, 18, 258));
    spn(path_builder_line_to(pb, 27, 260));
    spn(path_builder_line_to(pb, 18, 263));
    spn(path_builder_line_to(pb, 27, 265));
    spn(path_builder_line_to(pb, 19, 267));
    spn(path_builder_line_to(pb, 29, 270));
    spn(path_builder_line_to(pb, 21, 272));
    spn(path_builder_line_to(pb, 29, 276));
    spn(path_builder_line_to(pb, 21, 278));
    spn(path_builder_line_to(pb, 30, 281));
    spn(path_builder_line_to(pb, 22, 283));
    spn(path_builder_line_to(pb, 31, 287));
    spn(path_builder_line_to(pb, 24, 288));
    spn(path_builder_line_to(pb, 32, 292));
    spn(path_builder_line_to(pb, 23, 293));
    spn(path_builder_line_to(pb, 34, 298));
    spn(path_builder_line_to(pb, 26, 299));
    spn(path_builder_line_to(pb, 37, 303));
    spn(path_builder_line_to(pb, 32, 305));
    spn(path_builder_line_to(pb, 39, 309));
    spn(path_builder_line_to(pb, 33, 309));
    spn(path_builder_line_to(pb, 39, 314));
    spn(path_builder_line_to(pb, 34, 314));
    spn(path_builder_line_to(pb, 40, 318));
    spn(path_builder_line_to(pb, 34, 317));
    spn(path_builder_line_to(pb, 40, 321));
    spn(path_builder_line_to(pb, 34, 321));
    spn(path_builder_line_to(pb, 41, 326));
    spn(path_builder_line_to(pb, 33, 326));
    spn(path_builder_line_to(pb, 40, 330));
    spn(path_builder_line_to(pb, 33, 332));
    spn(path_builder_line_to(pb, 39, 333));
    spn(path_builder_line_to(pb, 33, 337));
    spn(path_builder_line_to(pb, 42, 337));
    spn(path_builder_line_to(pb, 54, 341));
    spn(path_builder_line_to(pb, 49, 337));
    spn(path_builder_line_to(pb, 52, 335));
    spn(path_builder_line_to(pb, 47, 330));
    spn(path_builder_line_to(pb, 50, 330));
    spn(path_builder_line_to(pb, 45, 325));
    spn(path_builder_line_to(pb, 49, 325));
    spn(path_builder_line_to(pb, 45, 321));
    spn(path_builder_line_to(pb, 48, 321));
    spn(path_builder_line_to(pb, 45, 316));
    spn(path_builder_line_to(pb, 46, 306));
    spn(path_builder_line_to(pb, 45, 286));
    spn(path_builder_line_to(pb, 43, 274));
    spn(path_builder_line_to(pb, 36, 261));
    spn(path_builder_line_to(pb, 33, 255));

    spn(path_builder_move_to(pb, 7, 358));
    spn(path_builder_line_to(pb, 9, 351));
    spn(path_builder_line_to(pb, 14, 351));
    spn(path_builder_line_to(pb, 17, 359));
    spn(path_builder_line_to(pb, 11, 364));
    spn(path_builder_line_to(pb, 7, 358));

    spn(path_builder_move_to(pb, 44, 354));
    spn(path_builder_line_to(pb, 49, 351));
    spn(path_builder_line_to(pb, 52, 355));
    spn(path_builder_line_to(pb, 49, 361));
    spn(path_builder_line_to(pb, 44, 354));

    spn(path_builder_move_to(pb, 32, 357));
    spn(path_builder_line_to(pb, 37, 353));
    spn(path_builder_line_to(pb, 40, 358));
    spn(path_builder_line_to(pb, 36, 361));
    spn(path_builder_line_to(pb, 32, 357));

    spn(path_builder_move_to(pb, 139, 334));
    spn(path_builder_line_to(pb, 145, 330));
    spn(path_builder_line_to(pb, 154, 330));
    spn(path_builder_line_to(pb, 158, 334));
    spn(path_builder_line_to(pb, 154, 341));
    spn(path_builder_line_to(pb, 152, 348));
    spn(path_builder_line_to(pb, 145, 350));
    spn(path_builder_line_to(pb, 149, 340));
    spn(path_builder_line_to(pb, 147, 336));
    spn(path_builder_line_to(pb, 141, 339));
    spn(path_builder_line_to(pb, 139, 345));
    spn(path_builder_line_to(pb, 136, 342));
    spn(path_builder_line_to(pb, 136, 339));
    spn(path_builder_line_to(pb, 139, 334));

    spn(path_builder_move_to(pb, 208, 259));
    spn(path_builder_line_to(pb, 215, 259));
    spn(path_builder_line_to(pb, 212, 255));
    spn(path_builder_line_to(pb, 220, 259));
    spn(path_builder_line_to(pb, 224, 263));
    spn(path_builder_line_to(pb, 225, 274));
    spn(path_builder_line_to(pb, 224, 283));
    spn(path_builder_line_to(pb, 220, 292));
    spn(path_builder_line_to(pb, 208, 300));
    spn(path_builder_line_to(pb, 206, 308));
    spn(path_builder_line_to(pb, 203, 304));
    spn(path_builder_line_to(pb, 199, 315));
    spn(path_builder_line_to(pb, 197, 309));
    spn(path_builder_line_to(pb, 195, 318));
    spn(path_builder_line_to(pb, 193, 313));
    spn(path_builder_line_to(pb, 190, 322));
    spn(path_builder_line_to(pb, 190, 316));
    spn(path_builder_line_to(pb, 185, 325));
    spn(path_builder_line_to(pb, 182, 318));
    spn(path_builder_line_to(pb, 180, 325));
    spn(path_builder_line_to(pb, 172, 321));
    spn(path_builder_line_to(pb, 178, 320));
    spn(path_builder_line_to(pb, 176, 313));
    spn(path_builder_line_to(pb, 186, 312));
    spn(path_builder_line_to(pb, 180, 307));
    spn(path_builder_line_to(pb, 188, 307));
    spn(path_builder_line_to(pb, 184, 303));
    spn(path_builder_line_to(pb, 191, 302));
    spn(path_builder_line_to(pb, 186, 299));
    spn(path_builder_line_to(pb, 195, 294));
    spn(path_builder_line_to(pb, 187, 290));
    spn(path_builder_line_to(pb, 197, 288));
    spn(path_builder_line_to(pb, 192, 286));
    spn(path_builder_line_to(pb, 201, 283));
    spn(path_builder_line_to(pb, 194, 280));
    spn(path_builder_line_to(pb, 203, 277));
    spn(path_builder_line_to(pb, 198, 275));
    spn(path_builder_line_to(pb, 207, 271));
    spn(path_builder_line_to(pb, 200, 269));
    spn(path_builder_line_to(pb, 209, 265));
    spn(path_builder_line_to(pb, 204, 265));
    spn(path_builder_line_to(pb, 212, 262));
    spn(path_builder_line_to(pb, 208, 259));

    spn(path_builder_move_to(pb, 106, 126));
    spn(path_builder_line_to(pb, 106, 131));
    spn(path_builder_line_to(pb, 109, 132));
    spn(path_builder_line_to(pb, 111, 134));
    spn(path_builder_line_to(pb, 115, 132));
    spn(path_builder_line_to(pb, 115, 135));
    spn(path_builder_line_to(pb, 119, 133));
    spn(path_builder_line_to(pb, 118, 137));
    spn(path_builder_line_to(pb, 123, 137));
    spn(path_builder_line_to(pb, 128, 137));
    spn(path_builder_line_to(pb, 133, 134));
    spn(path_builder_line_to(pb, 136, 130));
    spn(path_builder_line_to(pb, 136, 127));
    spn(path_builder_line_to(pb, 132, 124));
    spn(path_builder_line_to(pb, 118, 128));
    spn(path_builder_line_to(pb, 112, 128));
    spn(path_builder_line_to(pb, 106, 126));

    spn(path_builder_move_to(pb, 107, 114));
    spn(path_builder_line_to(pb, 101, 110));
    spn(path_builder_line_to(pb, 98, 102));
    spn(path_builder_line_to(pb, 105, 97));
    spn(path_builder_line_to(pb, 111, 98));
    spn(path_builder_line_to(pb, 119, 102));
    spn(path_builder_line_to(pb, 121, 108));
    spn(path_builder_line_to(pb, 118, 112));
    spn(path_builder_line_to(pb, 113, 115));
    spn(path_builder_line_to(pb, 107, 114));

    spn(path_builder_move_to(pb, 148, 106));
    spn(path_builder_line_to(pb, 145, 110));
    spn(path_builder_line_to(pb, 146, 116));
    spn(path_builder_line_to(pb, 150, 118));
    spn(path_builder_line_to(pb, 152, 111));
    spn(path_builder_line_to(pb, 151, 107));
    spn(path_builder_line_to(pb, 148, 106));

    spn(path_builder_move_to(pb, 80, 55));
    spn(path_builder_line_to(pb, 70, 52));
    spn(path_builder_line_to(pb, 75, 58));
    spn(path_builder_line_to(pb, 63, 57));
    spn(path_builder_line_to(pb, 72, 61));
    spn(path_builder_line_to(pb, 57, 61));
    spn(path_builder_line_to(pb, 67, 66));
    spn(path_builder_line_to(pb, 57, 67));
    spn(path_builder_line_to(pb, 62, 69));
    spn(path_builder_line_to(pb, 54, 71));
    spn(path_builder_line_to(pb, 61, 73));
    spn(path_builder_line_to(pb, 54, 77));
    spn(path_builder_line_to(pb, 63, 78));
    spn(path_builder_line_to(pb, 53, 85));
    spn(path_builder_line_to(pb, 60, 84));
    spn(path_builder_line_to(pb, 56, 90));
    spn(path_builder_line_to(pb, 69, 84));
    spn(path_builder_line_to(pb, 63, 82));
    spn(path_builder_line_to(pb, 75, 76));
    spn(path_builder_line_to(pb, 70, 75));
    spn(path_builder_line_to(pb, 77, 72));
    spn(path_builder_line_to(pb, 72, 71));
    spn(path_builder_line_to(pb, 78, 69));
    spn(path_builder_line_to(pb, 72, 66));
    spn(path_builder_line_to(pb, 81, 67));
    spn(path_builder_line_to(pb, 78, 64));
    spn(path_builder_line_to(pb, 82, 63));
    spn(path_builder_line_to(pb, 80, 60));
    spn(path_builder_line_to(pb, 86, 62));
    spn(path_builder_line_to(pb, 80, 55));

    spn(path_builder_move_to(pb, 87, 56));
    spn(path_builder_line_to(pb, 91, 52));
    spn(path_builder_line_to(pb, 96, 50));
    spn(path_builder_line_to(pb, 102, 56));
    spn(path_builder_line_to(pb, 98, 56));
    spn(path_builder_line_to(pb, 92, 60));
    spn(path_builder_line_to(pb, 87, 56));

    spn(path_builder_move_to(pb, 85, 68));
    spn(path_builder_line_to(pb, 89, 73));
    spn(path_builder_line_to(pb, 98, 76));
    spn(path_builder_line_to(pb, 106, 74));
    spn(path_builder_line_to(pb, 96, 73));
    spn(path_builder_line_to(pb, 91, 70));
    spn(path_builder_line_to(pb, 85, 68));

    spn(path_builder_move_to(pb, 115, 57));
    spn(path_builder_line_to(pb, 114, 64));
    spn(path_builder_line_to(pb, 111, 64));
    spn(path_builder_line_to(pb, 115, 75));
    spn(path_builder_line_to(pb, 122, 81));
    spn(path_builder_line_to(pb, 122, 74));
    spn(path_builder_line_to(pb, 126, 79));
    spn(path_builder_line_to(pb, 126, 74));
    spn(path_builder_line_to(pb, 131, 78));
    spn(path_builder_line_to(pb, 130, 72));
    spn(path_builder_line_to(pb, 133, 77));
    spn(path_builder_line_to(pb, 131, 68));
    spn(path_builder_line_to(pb, 126, 61));
    spn(path_builder_line_to(pb, 119, 57));
    spn(path_builder_line_to(pb, 115, 57));

    spn(path_builder_move_to(pb, 145, 48));
    spn(path_builder_line_to(pb, 143, 53));
    spn(path_builder_line_to(pb, 147, 59));
    spn(path_builder_line_to(pb, 151, 59));
    spn(path_builder_line_to(pb, 150, 55));
    spn(path_builder_line_to(pb, 145, 48));

    spn(path_builder_move_to(pb, 26, 22));
    spn(path_builder_line_to(pb, 34, 15));
    spn(path_builder_line_to(pb, 43, 10));
    spn(path_builder_line_to(pb, 52, 10));
    spn(path_builder_line_to(pb, 59, 16));
    spn(path_builder_line_to(pb, 47, 15));
    spn(path_builder_line_to(pb, 32, 22));
    spn(path_builder_line_to(pb, 26, 22));

    spn(path_builder_move_to(pb, 160, 19));
    spn(path_builder_line_to(pb, 152, 26));
    spn(path_builder_line_to(pb, 149, 34));
    spn(path_builder_line_to(pb, 154, 33));
    spn(path_builder_line_to(pb, 152, 30));
    spn(path_builder_line_to(pb, 157, 30));
    spn(path_builder_line_to(pb, 155, 26));
    spn(path_builder_line_to(pb, 158, 27));
    spn(path_builder_line_to(pb, 157, 23));
    spn(path_builder_line_to(pb, 161, 23));
    spn(path_builder_line_to(pb, 160, 19));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  // -------------------------------------------------------------------

  {
    spn(path_builder_begin(pb));

    spn(path_builder_move_to(pb, 98, 117));
    spn(path_builder_line_to(pb, 105, 122));
    spn(path_builder_line_to(pb, 109, 122));
    spn(path_builder_line_to(pb, 105, 117));
    spn(path_builder_line_to(pb, 113, 120));
    spn(path_builder_line_to(pb, 121, 120));
    spn(path_builder_line_to(pb, 130, 112));
    spn(path_builder_line_to(pb, 128, 108));
    spn(path_builder_line_to(pb, 123, 103));
    spn(path_builder_line_to(pb, 123, 99));
    spn(path_builder_line_to(pb, 128, 101));
    spn(path_builder_line_to(pb, 132, 106));
    spn(path_builder_line_to(pb, 135, 109));
    spn(path_builder_line_to(pb, 142, 105));
    spn(path_builder_line_to(pb, 142, 101));
    spn(path_builder_line_to(pb, 145, 101));
    spn(path_builder_line_to(pb, 145, 91));
    spn(path_builder_line_to(pb, 148, 101));
    spn(path_builder_line_to(pb, 145, 105));
    spn(path_builder_line_to(pb, 136, 112));
    spn(path_builder_line_to(pb, 135, 116));
    spn(path_builder_line_to(pb, 143, 124));
    spn(path_builder_line_to(pb, 148, 120));
    spn(path_builder_line_to(pb, 150, 122));
    spn(path_builder_line_to(pb, 142, 128));
    spn(path_builder_line_to(pb, 133, 122));
    spn(path_builder_line_to(pb, 121, 125));
    spn(path_builder_line_to(pb, 112, 126));
    spn(path_builder_line_to(pb, 103, 125));
    spn(path_builder_line_to(pb, 100, 129));
    spn(path_builder_line_to(pb, 96, 124));
    spn(path_builder_line_to(pb, 98, 117));

    spn(path_builder_move_to(pb, 146, 118));
    spn(path_builder_line_to(pb, 152, 118));
    spn(path_builder_line_to(pb, 152, 115));
    spn(path_builder_line_to(pb, 149, 115));
    spn(path_builder_line_to(pb, 146, 118));

    spn(path_builder_move_to(pb, 148, 112));
    spn(path_builder_line_to(pb, 154, 111));
    spn(path_builder_line_to(pb, 154, 109));
    spn(path_builder_line_to(pb, 149, 109));
    spn(path_builder_line_to(pb, 148, 112));

    spn(path_builder_move_to(pb, 106, 112));
    spn(path_builder_line_to(pb, 108, 115));
    spn(path_builder_line_to(pb, 114, 116));
    spn(path_builder_line_to(pb, 118, 114));
    spn(path_builder_line_to(pb, 106, 112));

    spn(path_builder_move_to(pb, 108, 108));
    spn(path_builder_line_to(pb, 111, 110));
    spn(path_builder_line_to(pb, 116, 110));
    spn(path_builder_line_to(pb, 119, 108));
    spn(path_builder_line_to(pb, 108, 108));

    spn(path_builder_move_to(pb, 106, 104));
    spn(path_builder_line_to(pb, 109, 105));
    spn(path_builder_line_to(pb, 117, 106));
    spn(path_builder_line_to(pb, 115, 104));
    spn(path_builder_line_to(pb, 106, 104));

    spn(path_builder_move_to(pb, 50, 25));
    spn(path_builder_line_to(pb, 41, 26));
    spn(path_builder_line_to(pb, 34, 33));
    spn(path_builder_line_to(pb, 39, 43));
    spn(path_builder_line_to(pb, 49, 58));
    spn(path_builder_line_to(pb, 36, 51));
    spn(path_builder_line_to(pb, 47, 68));
    spn(path_builder_line_to(pb, 55, 69));
    spn(path_builder_line_to(pb, 54, 59));
    spn(path_builder_line_to(pb, 61, 57));
    spn(path_builder_line_to(pb, 74, 46));
    spn(path_builder_line_to(pb, 60, 52));
    spn(path_builder_line_to(pb, 67, 42));
    spn(path_builder_line_to(pb, 57, 48));
    spn(path_builder_line_to(pb, 61, 40));
    spn(path_builder_line_to(pb, 54, 45));
    spn(path_builder_line_to(pb, 60, 36));
    spn(path_builder_line_to(pb, 59, 29));
    spn(path_builder_line_to(pb, 48, 38));
    spn(path_builder_line_to(pb, 52, 30));
    spn(path_builder_line_to(pb, 47, 32));
    spn(path_builder_line_to(pb, 50, 25));

    spn(path_builder_move_to(pb, 147, 34));
    spn(path_builder_line_to(pb, 152, 41));
    spn(path_builder_line_to(pb, 155, 49));
    spn(path_builder_line_to(pb, 161, 53));
    spn(path_builder_line_to(pb, 157, 47));
    spn(path_builder_line_to(pb, 164, 47));
    spn(path_builder_line_to(pb, 158, 43));
    spn(path_builder_line_to(pb, 168, 44));
    spn(path_builder_line_to(pb, 159, 40));
    spn(path_builder_line_to(pb, 164, 37));
    spn(path_builder_line_to(pb, 169, 37));
    spn(path_builder_line_to(pb, 164, 33));
    spn(path_builder_line_to(pb, 169, 34));
    spn(path_builder_line_to(pb, 165, 28));
    spn(path_builder_line_to(pb, 170, 30));
    spn(path_builder_line_to(pb, 170, 25));
    spn(path_builder_line_to(pb, 173, 29));
    spn(path_builder_line_to(pb, 175, 27));
    spn(path_builder_line_to(pb, 176, 32));
    spn(path_builder_line_to(pb, 173, 36));
    spn(path_builder_line_to(pb, 175, 39));
    spn(path_builder_line_to(pb, 172, 42));
    spn(path_builder_line_to(pb, 172, 46));
    spn(path_builder_line_to(pb, 168, 49));
    spn(path_builder_line_to(pb, 170, 55));
    spn(path_builder_line_to(pb, 162, 57));
    spn(path_builder_line_to(pb, 158, 63));
    spn(path_builder_line_to(pb, 155, 58));
    spn(path_builder_line_to(pb, 153, 50));
    spn(path_builder_line_to(pb, 149, 46));
    spn(path_builder_line_to(pb, 147, 34));

    spn(path_builder_move_to(pb, 155, 71));
    spn(path_builder_line_to(pb, 159, 80));
    spn(path_builder_line_to(pb, 157, 93));
    spn(path_builder_line_to(pb, 157, 102));
    spn(path_builder_line_to(pb, 155, 108));
    spn(path_builder_line_to(pb, 150, 101));
    spn(path_builder_line_to(pb, 149, 93));
    spn(path_builder_line_to(pb, 154, 101));
    spn(path_builder_line_to(pb, 152, 91));
    spn(path_builder_line_to(pb, 151, 83));
    spn(path_builder_line_to(pb, 155, 79));
    spn(path_builder_line_to(pb, 155, 71));

    spn(path_builder_move_to(pb, 112, 78));
    spn(path_builder_line_to(pb, 115, 81));
    spn(path_builder_line_to(pb, 114, 91));
    spn(path_builder_line_to(pb, 112, 87));
    spn(path_builder_line_to(pb, 113, 82));
    spn(path_builder_line_to(pb, 112, 78));

    spn(path_builder_move_to(pb, 78, 28));
    spn(path_builder_line_to(pb, 64, 17));
    spn(path_builder_line_to(pb, 58, 11));
    spn(path_builder_line_to(pb, 47, 9));
    spn(path_builder_line_to(pb, 36, 10));
    spn(path_builder_line_to(pb, 28, 16));
    spn(path_builder_line_to(pb, 21, 26));
    spn(path_builder_line_to(pb, 18, 41));
    spn(path_builder_line_to(pb, 20, 51));
    spn(path_builder_line_to(pb, 23, 61));
    spn(path_builder_line_to(pb, 33, 65));
    spn(path_builder_line_to(pb, 28, 68));
    spn(path_builder_line_to(pb, 37, 74));
    spn(path_builder_line_to(pb, 36, 81));
    spn(path_builder_line_to(pb, 43, 87));
    spn(path_builder_line_to(pb, 48, 90));
    spn(path_builder_line_to(pb, 43, 100));
    spn(path_builder_line_to(pb, 40, 98));
    spn(path_builder_line_to(pb, 39, 90));
    spn(path_builder_line_to(pb, 31, 80));
    spn(path_builder_line_to(pb, 30, 72));
    spn(path_builder_line_to(pb, 22, 71));
    spn(path_builder_line_to(pb, 17, 61));
    spn(path_builder_line_to(pb, 14, 46));
    spn(path_builder_line_to(pb, 16, 28));
    spn(path_builder_line_to(pb, 23, 17));
    spn(path_builder_line_to(pb, 33, 9));
    spn(path_builder_line_to(pb, 45, 6));
    spn(path_builder_line_to(pb, 54, 6));
    spn(path_builder_line_to(pb, 65, 12));
    spn(path_builder_line_to(pb, 78, 28));

    spn(path_builder_move_to(pb, 67, 18));
    spn(path_builder_line_to(pb, 76, 9));
    spn(path_builder_line_to(pb, 87, 5));
    spn(path_builder_line_to(pb, 101, 2));
    spn(path_builder_line_to(pb, 118, 3));
    spn(path_builder_line_to(pb, 135, 8));
    spn(path_builder_line_to(pb, 149, 20));
    spn(path_builder_line_to(pb, 149, 26));
    spn(path_builder_line_to(pb, 144, 19));
    spn(path_builder_line_to(pb, 132, 12));
    spn(path_builder_line_to(pb, 121, 9));
    spn(path_builder_line_to(pb, 105, 7));
    spn(path_builder_line_to(pb, 89, 8));
    spn(path_builder_line_to(pb, 76, 14));
    spn(path_builder_line_to(pb, 70, 20));
    spn(path_builder_line_to(pb, 67, 18));

    spn(path_builder_move_to(pb, 56, 98));
    spn(path_builder_line_to(pb, 48, 106));
    spn(path_builder_line_to(pb, 56, 103));
    spn(path_builder_line_to(pb, 47, 112));
    spn(path_builder_line_to(pb, 56, 110));
    spn(path_builder_line_to(pb, 52, 115));
    spn(path_builder_line_to(pb, 57, 113));
    spn(path_builder_line_to(pb, 52, 121));
    spn(path_builder_line_to(pb, 62, 115));
    spn(path_builder_line_to(pb, 58, 123));
    spn(path_builder_line_to(pb, 65, 119));
    spn(path_builder_line_to(pb, 63, 125));
    spn(path_builder_line_to(pb, 69, 121));
    spn(path_builder_line_to(pb, 68, 127));
    spn(path_builder_line_to(pb, 74, 125));
    spn(path_builder_line_to(pb, 74, 129));
    spn(path_builder_line_to(pb, 79, 128));
    spn(path_builder_line_to(pb, 83, 132));
    spn(path_builder_line_to(pb, 94, 135));
    spn(path_builder_line_to(pb, 93, 129));
    spn(path_builder_line_to(pb, 85, 127));
    spn(path_builder_line_to(pb, 81, 122));
    spn(path_builder_line_to(pb, 76, 126));
    spn(path_builder_line_to(pb, 75, 121));
    spn(path_builder_line_to(pb, 71, 124));
    spn(path_builder_line_to(pb, 71, 117));
    spn(path_builder_line_to(pb, 66, 121));
    spn(path_builder_line_to(pb, 66, 117));
    spn(path_builder_line_to(pb, 62, 117));
    spn(path_builder_line_to(pb, 64, 112));
    spn(path_builder_line_to(pb, 60, 113));
    spn(path_builder_line_to(pb, 60, 110));
    spn(path_builder_line_to(pb, 57, 111));
    spn(path_builder_line_to(pb, 61, 105));
    spn(path_builder_line_to(pb, 57, 107));
    spn(path_builder_line_to(pb, 60, 101));
    spn(path_builder_line_to(pb, 55, 102));
    spn(path_builder_line_to(pb, 56, 98));

    spn(path_builder_move_to(pb, 101, 132));
    spn(path_builder_line_to(pb, 103, 138));
    spn(path_builder_line_to(pb, 106, 134));
    spn(path_builder_line_to(pb, 106, 139));
    spn(path_builder_line_to(pb, 112, 136));
    spn(path_builder_line_to(pb, 111, 142));
    spn(path_builder_line_to(pb, 115, 139));
    spn(path_builder_line_to(pb, 114, 143));
    spn(path_builder_line_to(pb, 119, 142));
    spn(path_builder_line_to(pb, 125, 145));
    spn(path_builder_line_to(pb, 131, 142));
    spn(path_builder_line_to(pb, 135, 138));
    spn(path_builder_line_to(pb, 140, 134));
    spn(path_builder_line_to(pb, 140, 129));
    spn(path_builder_line_to(pb, 143, 135));
    spn(path_builder_line_to(pb, 145, 149));
    spn(path_builder_line_to(pb, 150, 171));
    spn(path_builder_line_to(pb, 149, 184));
    spn(path_builder_line_to(pb, 145, 165));
    spn(path_builder_line_to(pb, 141, 150));
    spn(path_builder_line_to(pb, 136, 147));
    spn(path_builder_line_to(pb, 132, 151));
    spn(path_builder_line_to(pb, 131, 149));
    spn(path_builder_line_to(pb, 126, 152));
    spn(path_builder_line_to(pb, 125, 150));
    spn(path_builder_line_to(pb, 121, 152));
    spn(path_builder_line_to(pb, 117, 148));
    spn(path_builder_line_to(pb, 111, 152));
    spn(path_builder_line_to(pb, 110, 148));
    spn(path_builder_line_to(pb, 105, 149));
    spn(path_builder_line_to(pb, 104, 145));
    spn(path_builder_line_to(pb, 98, 150));
    spn(path_builder_line_to(pb, 96, 138));
    spn(path_builder_line_to(pb, 94, 132));
    spn(path_builder_line_to(pb, 94, 130));
    spn(path_builder_line_to(pb, 98, 132));
    spn(path_builder_line_to(pb, 101, 132));

    spn(path_builder_move_to(pb, 41, 94));
    spn(path_builder_line_to(pb, 43, 99));
    spn(path_builder_line_to(pb, 35, 110));
    spn(path_builder_line_to(pb, 26, 134));
    spn(path_builder_line_to(pb, 18, 160));
    spn(path_builder_line_to(pb, 12, 185));
    spn(path_builder_line_to(pb, 12, 211));
    spn(path_builder_line_to(pb, 9, 230));
    spn(path_builder_line_to(pb, 3, 247));
    spn(path_builder_line_to(pb, 5, 236));
    spn(path_builder_line_to(pb, 7, 217));
    spn(path_builder_line_to(pb, 6, 190));
    spn(path_builder_line_to(pb, 12, 163));
    spn(path_builder_line_to(pb, 23, 132));
    spn(path_builder_line_to(pb, 32, 110));
    spn(path_builder_line_to(pb, 41, 94));

    spn(path_builder_move_to(pb, 32, 246));
    spn(path_builder_line_to(pb, 41, 250));
    spn(path_builder_line_to(pb, 50, 257));
    spn(path_builder_line_to(pb, 52, 267));
    spn(path_builder_line_to(pb, 53, 295));
    spn(path_builder_line_to(pb, 53, 323));
    spn(path_builder_line_to(pb, 59, 350));
    spn(path_builder_line_to(pb, 54, 363));
    spn(path_builder_line_to(pb, 51, 365));
    spn(path_builder_line_to(pb, 44, 366));
    spn(path_builder_line_to(pb, 42, 360));
    spn(path_builder_line_to(pb, 40, 372));
    spn(path_builder_line_to(pb, 54, 372));
    spn(path_builder_line_to(pb, 59, 366));
    spn(path_builder_line_to(pb, 62, 353));
    spn(path_builder_line_to(pb, 71, 352));
    spn(path_builder_line_to(pb, 75, 335));
    spn(path_builder_line_to(pb, 73, 330));
    spn(path_builder_line_to(pb, 66, 318));
    spn(path_builder_line_to(pb, 68, 302));
    spn(path_builder_line_to(pb, 64, 294));
    spn(path_builder_line_to(pb, 67, 288));
    spn(path_builder_line_to(pb, 63, 286));
    spn(path_builder_line_to(pb, 63, 279));
    spn(path_builder_line_to(pb, 59, 275));
    spn(path_builder_line_to(pb, 58, 267));
    spn(path_builder_line_to(pb, 56, 262));
    spn(path_builder_line_to(pb, 50, 247));
    spn(path_builder_line_to(pb, 42, 235));
    spn(path_builder_line_to(pb, 44, 246));
    spn(path_builder_line_to(pb, 32, 236));
    spn(path_builder_line_to(pb, 35, 244));
    spn(path_builder_line_to(pb, 32, 246));

    // THIS POLYLINE WAS REVERSED
    spn(path_builder_move_to(pb, 134, 324));
    spn(path_builder_line_to(pb, 134, 329));
    spn(path_builder_line_to(pb, 152, 326));
    spn(path_builder_line_to(pb, 163, 328));
    spn(path_builder_line_to(pb, 170, 333));
    spn(path_builder_line_to(pb, 174, 343));
    spn(path_builder_line_to(pb, 170, 350));
    spn(path_builder_line_to(pb, 158, 357));
    spn(path_builder_line_to(pb, 172, 355));
    spn(path_builder_line_to(pb, 179, 349));
    spn(path_builder_line_to(pb, 179, 337));
    spn(path_builder_line_to(pb, 173, 327));
    spn(path_builder_line_to(pb, 159, 322));
    spn(path_builder_line_to(pb, 146, 320));
    spn(path_builder_line_to(pb, 134, 324));

    // THIS POLYLINE WAS REVERSED
    spn(path_builder_move_to(pb, 173, 339));
    spn(path_builder_line_to(pb, 178, 348));
    spn(path_builder_line_to(pb, 200, 349));
    spn(path_builder_line_to(pb, 212, 342));
    spn(path_builder_line_to(pb, 218, 335));
    spn(path_builder_line_to(pb, 221, 323));
    spn(path_builder_line_to(pb, 232, 305));
    spn(path_builder_line_to(pb, 238, 291));
    spn(path_builder_line_to(pb, 238, 277));
    spn(path_builder_line_to(pb, 237, 265));
    spn(path_builder_line_to(pb, 232, 257));
    spn(path_builder_line_to(pb, 223, 250));
    spn(path_builder_line_to(pb, 210, 249));
    spn(path_builder_line_to(pb, 198, 252));
    spn(path_builder_line_to(pb, 208, 252));
    spn(path_builder_line_to(pb, 219, 253));
    spn(path_builder_line_to(pb, 225, 256));
    spn(path_builder_line_to(pb, 230, 262));
    spn(path_builder_line_to(pb, 233, 269));
    spn(path_builder_line_to(pb, 234, 279));
    spn(path_builder_line_to(pb, 232, 289));
    spn(path_builder_line_to(pb, 228, 296));
    spn(path_builder_line_to(pb, 221, 303));
    spn(path_builder_line_to(pb, 213, 309));
    spn(path_builder_line_to(pb, 209, 320));
    spn(path_builder_line_to(pb, 206, 318));
    spn(path_builder_line_to(pb, 202, 325));
    spn(path_builder_line_to(pb, 199, 323));
    spn(path_builder_line_to(pb, 194, 332));
    spn(path_builder_line_to(pb, 191, 329));
    spn(path_builder_line_to(pb, 184, 338));
    spn(path_builder_line_to(pb, 183, 334));
    spn(path_builder_line_to(pb, 173, 339));

    spn(path_builder_move_to(pb, 165, 296));
    spn(path_builder_line_to(pb, 158, 301));
    spn(path_builder_line_to(pb, 156, 310));
    spn(path_builder_line_to(pb, 156, 323));
    spn(path_builder_line_to(pb, 162, 324));
    spn(path_builder_line_to(pb, 159, 318));
    spn(path_builder_line_to(pb, 162, 308));
    spn(path_builder_line_to(pb, 162, 304));
    spn(path_builder_line_to(pb, 165, 296));

    spn(path_builder_move_to(pb, 99, 252));
    spn(path_builder_line_to(pb, 105, 244));
    spn(path_builder_line_to(pb, 107, 234));
    spn(path_builder_line_to(pb, 115, 228));
    spn(path_builder_line_to(pb, 121, 228));
    spn(path_builder_line_to(pb, 131, 235));
    spn(path_builder_line_to(pb, 122, 233));
    spn(path_builder_line_to(pb, 113, 235));
    spn(path_builder_line_to(pb, 109, 246));
    spn(path_builder_line_to(pb, 121, 239));
    spn(path_builder_line_to(pb, 133, 243));
    spn(path_builder_line_to(pb, 121, 243));
    spn(path_builder_line_to(pb, 110, 251));
    spn(path_builder_line_to(pb, 99, 252));

    spn(path_builder_move_to(pb, 117, 252));
    spn(path_builder_line_to(pb, 124, 247));
    spn(path_builder_line_to(pb, 134, 249));
    spn(path_builder_line_to(pb, 136, 253));
    spn(path_builder_line_to(pb, 126, 252));
    spn(path_builder_line_to(pb, 117, 252));

    spn(path_builder_move_to(pb, 117, 218));
    spn(path_builder_line_to(pb, 132, 224));
    spn(path_builder_line_to(pb, 144, 233));
    spn(path_builder_line_to(pb, 140, 225));
    spn(path_builder_line_to(pb, 132, 219));
    spn(path_builder_line_to(pb, 117, 218));

    spn(path_builder_move_to(pb, 122, 212));
    spn(path_builder_line_to(pb, 134, 214));
    spn(path_builder_line_to(pb, 143, 221));
    spn(path_builder_line_to(pb, 141, 213));
    spn(path_builder_line_to(pb, 132, 210));
    spn(path_builder_line_to(pb, 122, 212));

    spn(path_builder_move_to(pb, 69, 352));
    spn(path_builder_line_to(pb, 70, 363));
    spn(path_builder_line_to(pb, 76, 373));
    spn(path_builder_line_to(pb, 86, 378));
    spn(path_builder_line_to(pb, 97, 379));
    spn(path_builder_line_to(pb, 108, 379));
    spn(path_builder_line_to(pb, 120, 377));
    spn(path_builder_line_to(pb, 128, 378));
    spn(path_builder_line_to(pb, 132, 373));
    spn(path_builder_line_to(pb, 135, 361));
    spn(path_builder_line_to(pb, 133, 358));
    spn(path_builder_line_to(pb, 132, 366));
    spn(path_builder_line_to(pb, 127, 375));
    spn(path_builder_line_to(pb, 121, 374));
    spn(path_builder_line_to(pb, 121, 362));
    spn(path_builder_line_to(pb, 119, 367));
    spn(path_builder_line_to(pb, 117, 374));
    spn(path_builder_line_to(pb, 110, 376));
    spn(path_builder_line_to(pb, 110, 362));
    spn(path_builder_line_to(pb, 107, 357));
    spn(path_builder_line_to(pb, 106, 371));
    spn(path_builder_line_to(pb, 104, 375));
    spn(path_builder_line_to(pb, 97, 376));
    spn(path_builder_line_to(pb, 90, 375));
    spn(path_builder_line_to(pb, 90, 368));
    spn(path_builder_line_to(pb, 86, 362));
    spn(path_builder_line_to(pb, 83, 364));
    spn(path_builder_line_to(pb, 86, 369));
    spn(path_builder_line_to(pb, 85, 373));
    spn(path_builder_line_to(pb, 78, 370));
    spn(path_builder_line_to(pb, 73, 362));
    spn(path_builder_line_to(pb, 71, 351));
    spn(path_builder_line_to(pb, 69, 352));

    spn(path_builder_move_to(pb, 100, 360));
    spn(path_builder_line_to(pb, 96, 363));
    spn(path_builder_line_to(pb, 99, 369));
    spn(path_builder_line_to(pb, 102, 364));
    spn(path_builder_line_to(pb, 100, 360));

    spn(path_builder_move_to(pb, 115, 360));
    spn(path_builder_line_to(pb, 112, 363));
    spn(path_builder_line_to(pb, 114, 369));
    spn(path_builder_line_to(pb, 117, 364));
    spn(path_builder_line_to(pb, 115, 360));

    spn(path_builder_move_to(pb, 127, 362));
    spn(path_builder_line_to(pb, 125, 364));
    spn(path_builder_line_to(pb, 126, 369));
    spn(path_builder_line_to(pb, 128, 365));
    spn(path_builder_line_to(pb, 127, 362));

    spn(path_builder_move_to(pb, 5, 255));
    spn(path_builder_line_to(pb, 7, 276));
    spn(path_builder_line_to(pb, 11, 304));
    spn(path_builder_line_to(pb, 15, 320));
    spn(path_builder_line_to(pb, 13, 334));
    spn(path_builder_line_to(pb, 6, 348));
    spn(path_builder_line_to(pb, 2, 353));
    spn(path_builder_line_to(pb, 0, 363));
    spn(path_builder_line_to(pb, 5, 372));
    spn(path_builder_line_to(pb, 12, 374));
    spn(path_builder_line_to(pb, 25, 372));
    spn(path_builder_line_to(pb, 38, 372));
    spn(path_builder_line_to(pb, 44, 369));
    spn(path_builder_line_to(pb, 42, 367));
    spn(path_builder_line_to(pb, 36, 368));
    spn(path_builder_line_to(pb, 31, 369));
    spn(path_builder_line_to(pb, 30, 360));
    spn(path_builder_line_to(pb, 27, 368));
    spn(path_builder_line_to(pb, 20, 370));
    spn(path_builder_line_to(pb, 16, 361));
    spn(path_builder_line_to(pb, 15, 368));
    spn(path_builder_line_to(pb, 10, 369));
    spn(path_builder_line_to(pb, 3, 366));
    spn(path_builder_line_to(pb, 3, 359));
    spn(path_builder_line_to(pb, 6, 352));
    spn(path_builder_line_to(pb, 11, 348));
    spn(path_builder_line_to(pb, 17, 331));
    spn(path_builder_line_to(pb, 19, 316));
    spn(path_builder_line_to(pb, 12, 291));
    spn(path_builder_line_to(pb, 9, 274));
    spn(path_builder_line_to(pb, 5, 255));

    spn(path_builder_move_to(pb, 10, 358));
    spn(path_builder_line_to(pb, 7, 362));
    spn(path_builder_line_to(pb, 10, 366));
    spn(path_builder_line_to(pb, 11, 362));
    spn(path_builder_line_to(pb, 10, 358));

    spn(path_builder_move_to(pb, 25, 357));
    spn(path_builder_line_to(pb, 22, 360));
    spn(path_builder_line_to(pb, 24, 366));
    spn(path_builder_line_to(pb, 27, 360));
    spn(path_builder_line_to(pb, 25, 357));

    spn(path_builder_move_to(pb, 37, 357));
    spn(path_builder_line_to(pb, 34, 361));
    spn(path_builder_line_to(pb, 36, 365));
    spn(path_builder_line_to(pb, 38, 361));
    spn(path_builder_line_to(pb, 37, 357));

    spn(path_builder_move_to(pb, 49, 356));
    spn(path_builder_line_to(pb, 46, 359));
    spn(path_builder_line_to(pb, 47, 364));
    spn(path_builder_line_to(pb, 50, 360));
    spn(path_builder_line_to(pb, 49, 356));

    // THIS POLYLINE WAS REVERSED
    spn(path_builder_move_to(pb, 130, 101));
    spn(path_builder_line_to(pb, 133, 100));
    spn(path_builder_line_to(pb, 137, 100));
    spn(path_builder_line_to(pb, 142, 101));
    spn(path_builder_line_to(pb, 143, 103));
    spn(path_builder_line_to(pb, 139, 102));
    spn(path_builder_line_to(pb, 135, 101));
    spn(path_builder_line_to(pb, 132, 102));
    spn(path_builder_line_to(pb, 130, 101));

    spn(path_builder_move_to(pb, 106, 48));
    spn(path_builder_line_to(pb, 105, 52));
    spn(path_builder_line_to(pb, 108, 56));
    spn(path_builder_line_to(pb, 109, 52));
    spn(path_builder_line_to(pb, 106, 48));

    spn(path_builder_move_to(pb, 139, 52));
    spn(path_builder_line_to(pb, 139, 56));
    spn(path_builder_line_to(pb, 140, 60));
    spn(path_builder_line_to(pb, 142, 58));
    spn(path_builder_line_to(pb, 141, 56));
    spn(path_builder_line_to(pb, 139, 52));

    spn(path_builder_move_to(pb, 25, 349));
    spn(path_builder_line_to(pb, 29, 351));
    spn(path_builder_line_to(pb, 30, 355));
    spn(path_builder_line_to(pb, 33, 350));
    spn(path_builder_line_to(pb, 37, 348));
    spn(path_builder_line_to(pb, 42, 351));
    spn(path_builder_line_to(pb, 45, 347));
    spn(path_builder_line_to(pb, 49, 345));
    spn(path_builder_line_to(pb, 44, 343));
    spn(path_builder_line_to(pb, 36, 345));
    spn(path_builder_line_to(pb, 25, 349));

    spn(path_builder_move_to(pb, 98, 347));
    spn(path_builder_line_to(pb, 105, 351));
    spn(path_builder_line_to(pb, 107, 354));
    spn(path_builder_line_to(pb, 109, 349));
    spn(path_builder_line_to(pb, 115, 349));
    spn(path_builder_line_to(pb, 120, 353));
    spn(path_builder_line_to(pb, 118, 349));
    spn(path_builder_line_to(pb, 113, 346));
    spn(path_builder_line_to(pb, 104, 346));
    spn(path_builder_line_to(pb, 98, 347));

    spn(path_builder_move_to(pb, 83, 348));
    spn(path_builder_line_to(pb, 87, 352));
    spn(path_builder_line_to(pb, 87, 357));
    spn(path_builder_line_to(pb, 89, 351));
    spn(path_builder_line_to(pb, 87, 348));
    spn(path_builder_line_to(pb, 83, 348));

    spn(path_builder_move_to(pb, 155, 107));
    spn(path_builder_line_to(pb, 163, 107));
    spn(path_builder_line_to(pb, 170, 107));
    spn(path_builder_line_to(pb, 186, 108));
    spn(path_builder_line_to(pb, 175, 109));
    spn(path_builder_line_to(pb, 155, 109));
    spn(path_builder_line_to(pb, 155, 107));

    spn(path_builder_move_to(pb, 153, 114));
    spn(path_builder_line_to(pb, 162, 113));
    spn(path_builder_line_to(pb, 175, 112));
    spn(path_builder_line_to(pb, 192, 114));
    spn(path_builder_line_to(pb, 173, 114));
    spn(path_builder_line_to(pb, 154, 115));
    spn(path_builder_line_to(pb, 153, 114));

    spn(path_builder_move_to(pb, 152, 118));
    spn(path_builder_line_to(pb, 164, 120));
    spn(path_builder_line_to(pb, 180, 123));
    spn(path_builder_line_to(pb, 197, 129));
    spn(path_builder_line_to(pb, 169, 123));
    spn(path_builder_line_to(pb, 151, 120));
    spn(path_builder_line_to(pb, 152, 118));

    spn(path_builder_move_to(pb, 68, 109));
    spn(path_builder_line_to(pb, 87, 106));
    spn(path_builder_line_to(pb, 107, 106));
    spn(path_builder_line_to(pb, 106, 108));
    spn(path_builder_line_to(pb, 88, 108));
    spn(path_builder_line_to(pb, 68, 109));

    spn(path_builder_move_to(pb, 105, 111));
    spn(path_builder_line_to(pb, 95, 112));
    spn(path_builder_line_to(pb, 79, 114));
    spn(path_builder_line_to(pb, 71, 116));
    spn(path_builder_line_to(pb, 85, 115));
    spn(path_builder_line_to(pb, 102, 113));
    spn(path_builder_line_to(pb, 105, 111));

    spn(path_builder_move_to(pb, 108, 101));
    spn(path_builder_line_to(pb, 98, 99));
    spn(path_builder_line_to(pb, 87, 99));
    spn(path_builder_line_to(pb, 78, 99));
    spn(path_builder_line_to(pb, 93, 100));
    spn(path_builder_line_to(pb, 105, 102));
    spn(path_builder_line_to(pb, 108, 101));

    spn(path_builder_move_to(pb, 85, 63));
    spn(path_builder_line_to(pb, 91, 63));
    spn(path_builder_line_to(pb, 97, 60));
    spn(path_builder_line_to(pb, 104, 60));
    spn(path_builder_line_to(pb, 108, 62));
    spn(path_builder_line_to(pb, 111, 69));
    spn(path_builder_line_to(pb, 112, 75));
    spn(path_builder_line_to(pb, 110, 74));
    spn(path_builder_line_to(pb, 108, 71));
    spn(path_builder_line_to(pb, 103, 73));
    spn(path_builder_line_to(pb, 106, 69));
    spn(path_builder_line_to(pb, 105, 65));
    spn(path_builder_line_to(pb, 103, 64));
    spn(path_builder_line_to(pb, 103, 67));
    spn(path_builder_line_to(pb, 102, 70));
    spn(path_builder_line_to(pb, 99, 70));
    spn(path_builder_line_to(pb, 97, 66));
    spn(path_builder_line_to(pb, 94, 67));
    spn(path_builder_line_to(pb, 97, 72));
    spn(path_builder_line_to(pb, 88, 67));
    spn(path_builder_line_to(pb, 84, 66));
    spn(path_builder_line_to(pb, 85, 63));

    spn(path_builder_move_to(pb, 140, 74));
    spn(path_builder_line_to(pb, 141, 66));
    spn(path_builder_line_to(pb, 144, 61));
    spn(path_builder_line_to(pb, 150, 61));
    spn(path_builder_line_to(pb, 156, 62));
    spn(path_builder_line_to(pb, 153, 70));
    spn(path_builder_line_to(pb, 150, 73));
    spn(path_builder_line_to(pb, 152, 65));
    spn(path_builder_line_to(pb, 150, 65));
    spn(path_builder_line_to(pb, 151, 68));
    spn(path_builder_line_to(pb, 149, 71));
    spn(path_builder_line_to(pb, 146, 71));
    spn(path_builder_line_to(pb, 144, 66));
    spn(path_builder_line_to(pb, 143, 70));
    spn(path_builder_line_to(pb, 143, 74));
    spn(path_builder_line_to(pb, 140, 74));

    spn(path_builder_move_to(pb, 146, 20));
    spn(path_builder_line_to(pb, 156, 11));
    spn(path_builder_line_to(pb, 163, 9));
    spn(path_builder_line_to(pb, 172, 9));
    spn(path_builder_line_to(pb, 178, 14));
    spn(path_builder_line_to(pb, 182, 18));
    spn(path_builder_line_to(pb, 184, 32));
    spn(path_builder_line_to(pb, 182, 42));
    spn(path_builder_line_to(pb, 182, 52));
    spn(path_builder_line_to(pb, 177, 58));
    spn(path_builder_line_to(pb, 176, 67));
    spn(path_builder_line_to(pb, 171, 76));
    spn(path_builder_line_to(pb, 165, 90));
    spn(path_builder_line_to(pb, 157, 105));
    spn(path_builder_line_to(pb, 160, 92));
    spn(path_builder_line_to(pb, 164, 85));
    spn(path_builder_line_to(pb, 168, 78));
    spn(path_builder_line_to(pb, 167, 73));
    spn(path_builder_line_to(pb, 173, 66));
    spn(path_builder_line_to(pb, 172, 62));
    spn(path_builder_line_to(pb, 175, 59));
    spn(path_builder_line_to(pb, 174, 55));
    spn(path_builder_line_to(pb, 177, 53));
    spn(path_builder_line_to(pb, 180, 46));
    spn(path_builder_line_to(pb, 181, 29));
    spn(path_builder_line_to(pb, 179, 21));
    spn(path_builder_line_to(pb, 173, 13));
    spn(path_builder_line_to(pb, 166, 11));
    spn(path_builder_line_to(pb, 159, 13));
    spn(path_builder_line_to(pb, 153, 18));
    spn(path_builder_line_to(pb, 148, 23));
    spn(path_builder_line_to(pb, 146, 20));

    spn(path_builder_move_to(pb, 150, 187));
    spn(path_builder_line_to(pb, 148, 211));
    spn(path_builder_line_to(pb, 150, 233));
    spn(path_builder_line_to(pb, 153, 247));
    spn(path_builder_line_to(pb, 148, 267));
    spn(path_builder_line_to(pb, 135, 283));
    spn(path_builder_line_to(pb, 125, 299));
    spn(path_builder_line_to(pb, 136, 292));
    spn(path_builder_line_to(pb, 131, 313));
    spn(path_builder_line_to(pb, 122, 328));
    spn(path_builder_line_to(pb, 122, 345));
    spn(path_builder_line_to(pb, 129, 352));
    spn(path_builder_line_to(pb, 133, 359));
    spn(path_builder_line_to(pb, 133, 367));
    spn(path_builder_line_to(pb, 137, 359));
    spn(path_builder_line_to(pb, 148, 356));
    spn(path_builder_line_to(pb, 140, 350));
    spn(path_builder_line_to(pb, 131, 347));
    spn(path_builder_line_to(pb, 129, 340));
    spn(path_builder_line_to(pb, 132, 332));
    spn(path_builder_line_to(pb, 140, 328));
    spn(path_builder_line_to(pb, 137, 322));
    spn(path_builder_line_to(pb, 140, 304));
    spn(path_builder_line_to(pb, 154, 265));
    spn(path_builder_line_to(pb, 157, 244));
    spn(path_builder_line_to(pb, 155, 223));
    spn(path_builder_line_to(pb, 161, 220));
    spn(path_builder_line_to(pb, 175, 229));
    spn(path_builder_line_to(pb, 186, 247));
    spn(path_builder_line_to(pb, 185, 260));
    spn(path_builder_line_to(pb, 176, 275));
    spn(path_builder_line_to(pb, 178, 287));
    spn(path_builder_line_to(pb, 185, 277));
    spn(path_builder_line_to(pb, 188, 261));
    spn(path_builder_line_to(pb, 196, 253));
    spn(path_builder_line_to(pb, 189, 236));
    spn(path_builder_line_to(pb, 174, 213));
    spn(path_builder_line_to(pb, 150, 187));

    spn(path_builder_move_to(pb, 147, 338));
    spn(path_builder_line_to(pb, 142, 341));
    spn(path_builder_line_to(pb, 143, 345));
    spn(path_builder_line_to(pb, 141, 354));
    spn(path_builder_line_to(pb, 147, 343));
    spn(path_builder_line_to(pb, 147, 338));

    spn(path_builder_move_to(pb, 157, 342));
    spn(path_builder_line_to(pb, 156, 349));
    spn(path_builder_line_to(pb, 150, 356));
    spn(path_builder_line_to(pb, 157, 353));
    spn(path_builder_line_to(pb, 163, 346));
    spn(path_builder_line_to(pb, 162, 342));
    spn(path_builder_line_to(pb, 157, 342));

    // THIS POLYLINE WAS REVERSED
    spn(path_builder_move_to(pb, 99, 265));
    spn(path_builder_line_to(pb, 87, 300));
    spn(path_builder_line_to(pb, 73, 333));
    spn(path_builder_line_to(pb, 73, 339));
    spn(path_builder_line_to(pb, 92, 299));
    spn(path_builder_line_to(pb, 96, 284));
    spn(path_builder_line_to(pb, 99, 265));

    spn(path_builder_end(pb, paths + path_idx++));
  }

  *path_count = path_idx;

  return paths;
}

//
//
//

static spn_clip_t const spn_clip_default = {
  -FLT_MAX,  // lower left  corner of bounding box
  -FLT_MAX,
  +FLT_MAX,  // upper right corner of bounding box
  +FLT_MAX,
};

//
//
//

spn_raster_t *
lion_cub_rasters(spn_raster_builder_t           rb,
                 struct transform_stack * const ts,
                 uint32_t const                 rotations,
                 spn_path_t const * const       paths,
                 uint32_t const                 path_count,
                 uint32_t * const               raster_count)
{
  *raster_count = path_count * rotations;

  spn_raster_t * rasters = malloc(sizeof(*rasters) * *raster_count);

  float const theta_step = M_PI_F * 2.0f / (float)rotations;

#if 1
#define THETA_ROTATE(jj_) (theta_step * (float)(jj_))
#else
#define THETA_ROTATE(jj_) (theta_step * (float)0.0f)
#endif

  for (uint32_t jj = 0; jj < rotations; jj++)
    {
      // lion cub is 238 x 377
      transform_stack_push_translate(ts, 512.0f - 238.0f / 2.0f, 100.0f);
      transform_stack_concat(ts);
      transform_stack_push_rotate_xy(ts, THETA_ROTATE(jj), 238.0f / 2.0f, 377.0f + 50.0f);
      transform_stack_multiply(ts);

      for (uint32_t ii = 0; ii < path_count; ii++)
        {
          spn(raster_builder_begin(rb));

          spn(raster_builder_add(rb,
                                 paths + ii,
                                 NULL,
                                 (spn_transform_t *)transform_stack_top_transform(ts),
                                 NULL,
                                 &spn_clip_default,
                                 1));

          uint32_t const raster_idx = jj * path_count + ii;

          spn(raster_builder_end(rb, rasters + raster_idx));
        }

      transform_stack_drop(ts);
    }

  return rasters;
}

//
//
//

spn_layer_id *
lion_cub_composition(spn_composition_t          composition,
                     spn_raster_t const * const rasters,
                     uint32_t const             raster_count,
                     uint32_t * const           layer_count)
{
  spn_layer_id * layer_ids = malloc(sizeof(*layer_ids) * raster_count);

  *layer_count = raster_count;

  for (uint32_t ii = 0; ii < raster_count; ii++)
    {
      spn_layer_id const layer_id = raster_count - 1 - ii;

      layer_ids[ii] = layer_id;
    }

  spn(composition_place(composition, rasters, layer_ids, NULL, raster_count));

  return layer_ids;
}

//
//
//

void
lion_cub_styling(spn_styling_t              styling,
                 spn_group_id const         group_id,
                 spn_layer_id const * const layer_ids,
                 uint32_t const             layer_count)
{
  static uint32_t const colors[] = {

    0xF2CC99, 0xE5B27F, 0xEB8080, 0xF2CC99, 0x9C826B, 0xFFCC7F,
    0x9C826B, 0x845433, 0x9C826B, 0x000000, 0xFFE5B2, 0x000000
  };

  for (uint32_t ii = 0; ii < layer_count; ii++)
    {
      uint32_t const color_idx = ii % ARRAY_LENGTH_MACRO(colors);

      float rgba[4];

      color_rgb32_to_rgba_f32(rgba, colors[color_idx], 1.0f);

      // if (is_srgb)
      color_srgb_to_linear_rgb_f32(rgba);

      color_premultiply_rgba_f32(rgba);

      spn_styling_cmd_t * cmds;

      spn(styling_group_layer(styling, group_id, layer_ids[ii], 5, &cmds));

      cmds[0] = SPN_STYLING_OPCODE_COVER_NONZERO;

      // encode solid fill and fp16v4 at cmds[1-3]
      spn_styling_layer_fill_rgba_encoder(cmds + 1, rgba);

      cmds[4] = SPN_STYLING_OPCODE_BLEND_OVER;
    }
}
