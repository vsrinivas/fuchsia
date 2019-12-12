// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_spinel_test_utils.h"

#include "spinel/spinel_opcodes.h"
#include "tests/common/list_ostream.h"
#include "tests/common/spinel/spinel_test_utils.h"

namespace mock_spinel {

// static
spinel_api::Interface * Test::previous_interface_ = nullptr;

// static
Spinel * Test::spinel_ = nullptr;

///
///  Path
///

std::ostream &
operator<<(std::ostream & os, const Path & path)
{
  os << "MockPath[";
  // clang-format off
  list_ostream ls(os);
  for (const Path::Element& e : path) {
    if (e.any.tag == Path::MoveTo::kTag) {
      auto m = e.move_to;
      ls << "MoveTo(x:" << m.x << ",y:" << m.y << ")";
    }
    else if (e.any.tag == Path::LineTo::kTag) {
      auto m = e.line_to;
      ls << "LineTo(x:" << m.x << ",y:" << m.y << ")";
    }
    else if (e.any.tag == Path::QuadTo::kTag) {
      auto m = e.quad_to;
      ls << "QuadTo" 
            << "(cx:" << m.cx << ",cy:" << m.cy
            << ",x:"  << m.x  << ",y:"  << m.y << ")";
    }
    else if (e.any.tag == Path::CubicTo::kTag) {
      auto m = e.cubic_to;
      ls << "CubicTo("
            << "(c1x:" << m.c1x << ",c1y:" << m.c1y
            << ",c2x:" << m.c2x << ",c2y:" << m.c2y
            << ",x:"   << m.x   << ",y:"   << m.y << ")";
    }
    else if (e.any.tag == Path::RatQuadTo::kTag) {
        auto m = e.ratquad_to;
        ls << "RatQuadTo"
              << "(cx:" << m.cx << ",cy:" << m.cy
              << ",x:"  << m.x  << ",y:"  << m.y
              << ",w:"  << m.w  << ")";
    }
    else if (e.any.tag == Path::RatCubicTo::kTag) {
      auto m = e.ratcubic_to;
      ls << "RatCubicTo(c1x:" << m.c1x << ",c1y:" << m.c1y
            << "(c1x:" << m.c1x << ",c1y:" << m.c1y
            << ",c2x:" << m.c2x << ",c2y:" << m.c2y
            << ",x:"   << m.x   << ",y:"   << m.y
            << ",w1:"  << m.w1  << ",w2:"  << m.w2 << ")";
    }
    else {
      ls << "UNKNOWN(" << e.any.tag << ")";
    }
    ls << ls.comma;
  }
  // clang-format on
  os << "]";
  return os;
}

///
///  Raster
///

std::ostream &
operator<<(std::ostream & os, const RasterPath & raster_path)
{
  os << "MockRasterPath[id:" << raster_path.path_id << "," << raster_path.transform << ","
     << raster_path.clip << "]";
  return os;
}

std::ostream &
operator<<(std::ostream & os, const Raster & raster)
{
  os << "MockRaster[";
  list_ostream ls(os);
  for (const RasterPath & raster_path : raster)
    ls << raster_path << ls.comma;
  os << "]";
  return os;
}

std::ostream &
operator<<(std::ostream & os, const Composition & composition)
{
  os << "MockComposition[";
  auto         layerMap = composition.computeLayerMap();
  list_ostream ls(os);
  for (const auto it : layerMap)
    {
      uint32_t layer_id = it.first;
      ls << "Layer[id:" << layer_id << ",";
      list_ostream ls2(os);
      for (const RasterPrint * print : it.second)
        ls2 << "(raster_id:" << print->raster_id << "," << print->translation << ")" << ls2.comma;
      ls << "]" << ls.comma;
    }
  os << "]";
  return os;
}

static std::string
toString(const StylingCommands & cmds)
{
  std::stringstream ss;
  ss << "(count:" << cmds.size();
  ss << spinelStylingCommandsToString(&cmds[0], &cmds[cmds.size()]);
  ss << ")";
  return ss.str();
}

std::ostream &
operator<<(std::ostream & os, const StylingGroup & group)
{
  os << "layer_lo:" << group.layer_lo << ",layer_hi:" << group.layer_hi;
  if (!group.begin_commands.empty())
    os << "enter_cmds:" << toString(group.begin_commands);
  for (const auto & it : group.layer_commands)
    os << ",layer_cmds[" << it.first << "]:" << toString(it.second);
  if (!group.end_commands.empty())
    os << ",leave_cmds:" << toString(group.end_commands);
  return os;
}

std::ostream &
operator<<(std::ostream & os, const Styling & styling)
{
  os << "MockStyling[";
  list_ostream ls(os);
  for (size_t nn = 0; nn < styling.groups().size(); ++nn)
    ls << "[group_id:" << nn << "," << styling.groups()[nn] << "]" << ls.comma;
  os << "]";
  return os;
}

}  // namespace mock_spinel
