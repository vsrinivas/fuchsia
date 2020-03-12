// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_PATH_SINK_TEST_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_PATH_SINK_TEST_UTILS_H_

#include <sstream>
#include <string>
#include <vector>

#include "tests/common/path_sink.h"

// A PathSink implementation that records its calls.
//
// Usage is:
//   1) Create instance
//   2) Use it to build path objects.
//   3) Look at |commands| for the list of recorded commands.
//
class RecordingPathSink : public PathSink {
 public:
  enum CommandType
  {
    BEGIN,
    ADD_ITEM,
    END,
  };

  struct Command
  {
    CommandType type;
    struct
    {
      ItemType item_type;
      char     count;
      double   coords[kMaxCoords];
    } item;

    // Convert to std::string.
    std::string
    to_string() const
    {
      switch (type)
        {
          case BEGIN:
            return "BEGIN";
            case ADD_ITEM: {
              std::ostringstream os;
              switch (item.item_type)
                {
                  case MOVE_TO:
                    os << "MOVE_TO(";
                    break;
                  case LINE_TO:
                    os << "LINE_TO(";
                    break;
                  case QUAD_TO:
                    os << "QUAD_TO(";
                    break;
                  case CUBIC_TO:
                    os << "CUBIC_TO(";
                    break;
                  case RAT_QUAD_TO:
                    os << "RAT_QUAD_TO(";
                    break;
                  case RAT_CUBIC_TO:
                    os << "RAT_CUBIC_TO(";
                    break;
                  default:;
                }
              for (int nn = 0; nn < item.count; ++nn)
                {
                  if (nn > 0)
                    os << ' ';
                  double v = item.coords[nn];
                  if (fabs(v) < 1e-9)
                    v = 0.;
                  os << v;
                }
              os << ')';
              return os.str();
            }
          case END:
            return "END";
        }
    }
  };

  void
  begin() override
  {
    commands.push_back((Command){ .type = BEGIN });
  }

  void
  addItem(ItemType item_type, const double * coords) override
  {
    Command cmd = {
      .type = ADD_ITEM,
      .item = {
        .item_type = item_type,
        .count     = kArgsPerItemType[item_type],
      },
    };
    for (int nn = 0; nn < cmd.item.count; ++nn)
      cmd.item.coords[nn] = coords[nn];

    commands.push_back(cmd);
  }

  bool
  end() override
  {
    commands.push_back((Command){ .type = END });
    return true;
  }

  std::string
  to_string() const
  {
    std::string result;
    for (size_t nn = 0; nn < commands.size(); ++nn)
      {
        if (nn > 0)
          result += ";";
        result += commands[nn].to_string();
      }
    return result;
  }

  std::vector<Command> commands;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_PATH_SINK_TEST_UTILS_H_
