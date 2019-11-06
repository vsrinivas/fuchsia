// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_color_names.h"

#include <stdlib.h>
#include <string.h>

#include "common/macros.h"

//
//
//

#define SVG_COLOR_NAMES_LUT_ENTRY(n_, c_)                                                          \
  {                                                                                                \
    STRINGIFY_MACRO(n_), c_                                                                        \
  }

//
//
//

static struct svg_color_name const svg_color_names_lut[] = {
  // clang-format off
  SVG_COLOR_NAMES_LUT_ENTRY(aliceblue,             SVG_RGB(240, 248, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(antiquewhite,          SVG_RGB(250, 235, 215)),
  SVG_COLOR_NAMES_LUT_ENTRY(aqua,                  SVG_RGB(  0, 255, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(aquamarine,            SVG_RGB(127, 255, 212)),
  SVG_COLOR_NAMES_LUT_ENTRY(azure,                 SVG_RGB(240, 255, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(beige,                 SVG_RGB(245, 245, 220)),
  SVG_COLOR_NAMES_LUT_ENTRY(bisque,                SVG_RGB(255, 228, 196)),
  SVG_COLOR_NAMES_LUT_ENTRY(black,                 SVG_RGB(  0,   0,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(blanchedalmond,        SVG_RGB(255, 235, 205)),
  SVG_COLOR_NAMES_LUT_ENTRY(blue,                  SVG_RGB(  0,   0, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(blueviolet,            SVG_RGB(138,  43, 226)),
  SVG_COLOR_NAMES_LUT_ENTRY(brown,                 SVG_RGB(165,  42,   42)),
  SVG_COLOR_NAMES_LUT_ENTRY(burlywood,             SVG_RGB(222, 184, 135)),
  SVG_COLOR_NAMES_LUT_ENTRY(cadetblue,             SVG_RGB( 95, 158, 160)),
  SVG_COLOR_NAMES_LUT_ENTRY(chartreuse,            SVG_RGB(127, 255,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(chocolate,             SVG_RGB(210, 105,  30)),
  SVG_COLOR_NAMES_LUT_ENTRY(coral,                 SVG_RGB(255, 127,  80)),
  SVG_COLOR_NAMES_LUT_ENTRY(cornflowerblue,        SVG_RGB(100, 149, 237)),
  SVG_COLOR_NAMES_LUT_ENTRY(cornsilk,              SVG_RGB(255, 248, 220)),
  SVG_COLOR_NAMES_LUT_ENTRY(crimson,               SVG_RGB(220,  20,  60)),
  SVG_COLOR_NAMES_LUT_ENTRY(cyan,                  SVG_RGB(  0, 255, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkblue,              SVG_RGB(  0,   0, 139)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkcyan,              SVG_RGB(  0, 139, 139)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkgoldenrod,         SVG_RGB(184, 134,  11)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkgray,              SVG_RGB(169, 169, 169)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkgreen,             SVG_RGB(  0, 100,  0)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkgrey,              SVG_RGB(169, 169, 169)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkkhaki,             SVG_RGB(189, 183, 107)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkmagenta,           SVG_RGB(139,   0, 139)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkolivegreen,        SVG_RGB( 85, 107,  47)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkorange,            SVG_RGB(255, 140,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkorchid,            SVG_RGB(153,  50, 204)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkred,               SVG_RGB(139,   0,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(darksalmon,            SVG_RGB(233, 150, 122)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkseagreen,          SVG_RGB(143, 188, 143)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkslateblue,         SVG_RGB( 72,  61, 139)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkslategray,         SVG_RGB( 47,  79,  79)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkslategrey,         SVG_RGB( 47,  79,  79)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkturquoise,         SVG_RGB(  0, 206, 209)),
  SVG_COLOR_NAMES_LUT_ENTRY(darkviolet,            SVG_RGB(148,   0, 211)),
  SVG_COLOR_NAMES_LUT_ENTRY(deeppink,              SVG_RGB(255,  20, 147)),
  SVG_COLOR_NAMES_LUT_ENTRY(deepskyblue,           SVG_RGB(  0, 191, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(dimgray,               SVG_RGB(105, 105, 105)),
  SVG_COLOR_NAMES_LUT_ENTRY(dimgrey,               SVG_RGB(105, 105, 105)),
  SVG_COLOR_NAMES_LUT_ENTRY(dodgerblue,            SVG_RGB( 30, 144, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(firebrick,             SVG_RGB(178,  34,  34)),
  SVG_COLOR_NAMES_LUT_ENTRY(floralwhite,           SVG_RGB(255, 250, 240)),
  SVG_COLOR_NAMES_LUT_ENTRY(forestgreen,           SVG_RGB( 34, 139,  34)),
  SVG_COLOR_NAMES_LUT_ENTRY(fuchsia,               SVG_RGB(255,   0, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(gainsboro,             SVG_RGB(220, 220, 220)),
  SVG_COLOR_NAMES_LUT_ENTRY(ghostwhite,            SVG_RGB(248, 248, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(gold,                  SVG_RGB(255, 215,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(goldenrod,             SVG_RGB(218, 165, 32)),
  SVG_COLOR_NAMES_LUT_ENTRY(gray,                  SVG_RGB(128, 128, 128)),
  SVG_COLOR_NAMES_LUT_ENTRY(green,                 SVG_RGB(  0, 128,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(greenyellow,           SVG_RGB(173, 255,  47)),
  SVG_COLOR_NAMES_LUT_ENTRY(grey,                  SVG_RGB(128, 128, 128)),
  SVG_COLOR_NAMES_LUT_ENTRY(honeydew,              SVG_RGB(240, 255, 240)),
  SVG_COLOR_NAMES_LUT_ENTRY(hotpink,               SVG_RGB(255, 105, 180)),
  SVG_COLOR_NAMES_LUT_ENTRY(indianred,             SVG_RGB(205,  92, 92)),
  SVG_COLOR_NAMES_LUT_ENTRY(indigo,                SVG_RGB( 75,   0, 130)),
  SVG_COLOR_NAMES_LUT_ENTRY(ivory,                 SVG_RGB(255, 255, 240)),
  SVG_COLOR_NAMES_LUT_ENTRY(khaki,                 SVG_RGB(240, 230, 140)),
  SVG_COLOR_NAMES_LUT_ENTRY(lavender,              SVG_RGB(230, 230, 250)),
  SVG_COLOR_NAMES_LUT_ENTRY(lavenderblush,         SVG_RGB(255, 240, 245)),
  SVG_COLOR_NAMES_LUT_ENTRY(lawngreen,             SVG_RGB(124, 252,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(lemonchiffon,          SVG_RGB(255, 250, 205)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightblue,             SVG_RGB(173, 216, 230)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightcoral,            SVG_RGB(240, 128, 128)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightcyan,             SVG_RGB(224, 255, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightgoldenrodyellow,  SVG_RGB(250, 250, 210)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightgray,             SVG_RGB(211, 211, 211)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightgreen,            SVG_RGB(144, 238, 144)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightgrey,             SVG_RGB(211, 211, 211)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightpink,             SVG_RGB(255, 182, 193)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightsalmon,           SVG_RGB(255, 160, 122)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightseagreen,         SVG_RGB( 32, 178, 170)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightskyblue,          SVG_RGB(135, 206, 250)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightslategray,        SVG_RGB(119, 136, 153)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightslategrey,        SVG_RGB(119, 136, 153)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightsteelblue,        SVG_RGB(176, 196, 222)),
  SVG_COLOR_NAMES_LUT_ENTRY(lightyellow,           SVG_RGB(255, 255, 224)),
  SVG_COLOR_NAMES_LUT_ENTRY(lime,                  SVG_RGB(  0, 255,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(limegreen,             SVG_RGB( 50, 205, 50)),
  SVG_COLOR_NAMES_LUT_ENTRY(linen,                 SVG_RGB(250, 240, 230)),
  SVG_COLOR_NAMES_LUT_ENTRY(magenta,               SVG_RGB(255,   0, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(maroon,                SVG_RGB(128,   0,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumaquamarine,      SVG_RGB(102, 205, 170)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumblue,            SVG_RGB(  0,   0, 205)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumorchid,          SVG_RGB(186,  85, 211)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumpurple,          SVG_RGB(147, 112, 219)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumseagreen,        SVG_RGB( 60, 179, 113)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumslateblue,       SVG_RGB(123, 104, 238)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumspringgreen,     SVG_RGB(  0, 250, 154)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumturquoise,       SVG_RGB( 72, 209, 204)),
  SVG_COLOR_NAMES_LUT_ENTRY(mediumvioletred,       SVG_RGB(199,  21, 133)),
  SVG_COLOR_NAMES_LUT_ENTRY(midnightblue,          SVG_RGB( 25,  25, 112)),
  SVG_COLOR_NAMES_LUT_ENTRY(mintcream,             SVG_RGB(245, 255, 250)),
  SVG_COLOR_NAMES_LUT_ENTRY(mistyrose,             SVG_RGB(255, 228, 225)),
  SVG_COLOR_NAMES_LUT_ENTRY(moccasin,              SVG_RGB(255, 228, 181)),
  SVG_COLOR_NAMES_LUT_ENTRY(navajowhite,           SVG_RGB(255, 222, 173)),
  SVG_COLOR_NAMES_LUT_ENTRY(navy,                  SVG_RGB(  0,   0, 128)),
  SVG_COLOR_NAMES_LUT_ENTRY(oldlace,               SVG_RGB(253, 245, 230)),
  SVG_COLOR_NAMES_LUT_ENTRY(olive,                 SVG_RGB(128, 128,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(olivedrab,             SVG_RGB(107, 142,  35)),
  SVG_COLOR_NAMES_LUT_ENTRY(orange,                SVG_RGB(255, 165,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(orangered,             SVG_RGB(255,  69,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(orchid,                SVG_RGB(218, 112, 214)),
  SVG_COLOR_NAMES_LUT_ENTRY(palegoldenrod,         SVG_RGB(238, 232, 170)),
  SVG_COLOR_NAMES_LUT_ENTRY(palegreen,             SVG_RGB(152, 251, 152)),
  SVG_COLOR_NAMES_LUT_ENTRY(paleturquoise,         SVG_RGB(175, 238, 238)),
  SVG_COLOR_NAMES_LUT_ENTRY(palevioletred,         SVG_RGB(219, 112, 147)),
  SVG_COLOR_NAMES_LUT_ENTRY(papayawhip,            SVG_RGB(255, 239, 213)),
  SVG_COLOR_NAMES_LUT_ENTRY(peachpuff,             SVG_RGB(255, 218, 185)),
  SVG_COLOR_NAMES_LUT_ENTRY(peru,                  SVG_RGB(205, 133, 63)),
  SVG_COLOR_NAMES_LUT_ENTRY(pink,                  SVG_RGB(255, 192, 203)),
  SVG_COLOR_NAMES_LUT_ENTRY(plum,                  SVG_RGB(221, 160, 221)),
  SVG_COLOR_NAMES_LUT_ENTRY(powderblue,            SVG_RGB(176, 224, 230)),
  SVG_COLOR_NAMES_LUT_ENTRY(purple,                SVG_RGB(128,   0, 128)),
  SVG_COLOR_NAMES_LUT_ENTRY(red,                   SVG_RGB(255,   0,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(rosybrown,             SVG_RGB(188, 143, 143)),
  SVG_COLOR_NAMES_LUT_ENTRY(royalblue,             SVG_RGB( 65, 105, 225)),
  SVG_COLOR_NAMES_LUT_ENTRY(saddlebrown,           SVG_RGB(139,  69,  19)),
  SVG_COLOR_NAMES_LUT_ENTRY(salmon,                SVG_RGB(250, 128, 114)),
  SVG_COLOR_NAMES_LUT_ENTRY(sandybrown,            SVG_RGB(244, 164,  96)),
  SVG_COLOR_NAMES_LUT_ENTRY(seagreen,              SVG_RGB( 46, 139,  87)),
  SVG_COLOR_NAMES_LUT_ENTRY(seashell,              SVG_RGB(255, 245, 238)),
  SVG_COLOR_NAMES_LUT_ENTRY(sienna,                SVG_RGB(160,  82,  45)),
  SVG_COLOR_NAMES_LUT_ENTRY(silver,                SVG_RGB(192, 192, 192)),
  SVG_COLOR_NAMES_LUT_ENTRY(skyblue,               SVG_RGB(135, 206, 235)),
  SVG_COLOR_NAMES_LUT_ENTRY(slateblue,             SVG_RGB(106,  90, 205)),
  SVG_COLOR_NAMES_LUT_ENTRY(slategray,             SVG_RGB(112, 128, 144)),
  SVG_COLOR_NAMES_LUT_ENTRY(slategrey,             SVG_RGB(112, 128, 144)),
  SVG_COLOR_NAMES_LUT_ENTRY(snow,                  SVG_RGB(255, 250, 250)),
  SVG_COLOR_NAMES_LUT_ENTRY(springgreen,           SVG_RGB(  0, 255, 127)),
  SVG_COLOR_NAMES_LUT_ENTRY(steelblue,             SVG_RGB( 70, 130, 180)),
  SVG_COLOR_NAMES_LUT_ENTRY(tan,                   SVG_RGB(210, 180, 140)),
  SVG_COLOR_NAMES_LUT_ENTRY(teal,                  SVG_RGB(  0, 128, 128)),
  SVG_COLOR_NAMES_LUT_ENTRY(thistle,               SVG_RGB(216, 191, 216)),
  SVG_COLOR_NAMES_LUT_ENTRY(tomato,                SVG_RGB(255,  99,  71)),
  SVG_COLOR_NAMES_LUT_ENTRY(turquoise,             SVG_RGB( 64, 224, 208)),
  SVG_COLOR_NAMES_LUT_ENTRY(violet,                SVG_RGB(238, 130, 238)),
  SVG_COLOR_NAMES_LUT_ENTRY(wheat,                 SVG_RGB(245, 222, 179)),
  SVG_COLOR_NAMES_LUT_ENTRY(white,                 SVG_RGB(255, 255, 255)),
  SVG_COLOR_NAMES_LUT_ENTRY(whitesmoke,            SVG_RGB(245, 245, 245)),
  SVG_COLOR_NAMES_LUT_ENTRY(yellow,                SVG_RGB(255, 255,   0)),
  SVG_COLOR_NAMES_LUT_ENTRY(yellowgreen,           SVG_RGB(154, 205,  50))
  // clang-format on
};

//
//
//

static int
svg_color_name_cmp(void const * l, void const * r)
{
  char const * const                  ls = l;
  struct svg_color_name const * const rs = r;

  return strcmp(ls, rs->name);
}

struct svg_color_name const *
svg_color_name_lookup(char const * str, uint32_t len)
{
  //
  // FIXME(allanmac): this used to use a perfect hash
  //
  return bsearch(str,
                 svg_color_names_lut,
                 ARRAY_LENGTH_MACRO(svg_color_names_lut),
                 sizeof(svg_color_names_lut[0]),
                 svg_color_name_cmp);
}

//
//
//
