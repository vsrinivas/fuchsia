// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_H_

#include "lib/fit/function.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class BreakpointLocation;
class Err;
class Target;
class Thread;

class Breakpoint : public ClientObject {
 public:
  explicit Breakpoint(Session* session);
  ~Breakpoint() override;

  fxl::WeakPtr<Breakpoint> GetWeakPtr();

  // All of the settings, including the location, are stored in the BreakpointSettings object. This
  // API is designed so all settings changes happen atomically. SetSettings() will always issue the
  // callback, even if the breakpoint has been destroyed. If you need to reference the breakpoint
  // object in the callback, get a weak pointer.
  virtual BreakpointSettings GetSettings() const = 0;
  virtual void SetSettings(const BreakpointSettings& settings,
                           fit::callback<void(const Err&)> callback) = 0;

  // Returns true if this is an internal breakpoint. Internal breakpoints are used to implement
  // other operations and are never exposed to the user.
  virtual bool IsInternal() const = 0;

  // Returns the locations associated with this breakpoint. These are the actual addresses set. The
  // symbols of these may not match the one in the settings (for example, the line number might be
  // different due to optimization for each location).
  //
  // The returned pointers are owned by the Breakpoint and will be changed if the settings or any
  // process or module changes take place. Don't cache.
  //
  // This function has a non-const variant so callers can enable or disable individual locations
  // using the resulting pointers.
  virtual std::vector<const BreakpointLocation*> GetLocations() const = 0;
  virtual std::vector<BreakpointLocation*> GetLocations() = 0;

  SettingStore& settings() { return settings_; }
  static fxl::RefPtr<SettingSchema> GetSchema();

 private:
  // Implements the SettingStore interface for the Breakpoint (uses composition instead of
  // inheritance to keep the Breakpoint API simpler).
  class Settings : public SettingStore {
   public:
    explicit Settings(Breakpoint* bp);

   protected:
    SettingValue GetStorageValue(const std::string& key) const override;
    Err SetStorageValue(const std::string& key, SettingValue value) override;

   private:
    Breakpoint* bp_;  // Object that owns us.
  };
  friend Settings;

  Settings settings_;

  fxl::WeakPtrFactory<Breakpoint> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Breakpoint);
};

// We want to display the menu of types in various places. This macro expands to that. The "indent"
// string is prepended to every line so the left can be indented as needed for the user.
// clang-format off
#define BREAKPOINT_TYPE_HELP(indent)                                                     \
    indent "software\n"                                                                  \
    indent "    Software execution breakpoint. This is a \"normal\" breakpoint where\n"  \
    indent "    the instruction in memory is replaced with an explicit \"break\"\n"      \
    indent "    instruction.\n"                                                          \
           "\n"                                                                          \
    indent "execute\n"                                                                   \
    indent "    Hardware execution breakpoint. This sets a CPU register to stop\n"       \
    indent "    execution when the address is executed. The advantages are that\n"       \
    indent "    this can be done without modifying memory and that per-thread\n"         \
    indent "    breakpoints are more efficient. The disadvantage is that there\n"        \
    indent "    are a limited number of hardware breakpoints.\n"                         \
           "\n"                                                                          \
    indent "read-write\n"                                                                \
    indent "    Hardware read/write breakpoint. Sets a CPU register to break\n"          \
    indent "    whenever the data at the address is read or written.\n"                  \
           "\n"                                                                          \
    indent "write\n"                                                                     \
    indent "    Hardware write breakpoint. Sets a CPU register to break whenever\n"      \
    indent "    the data at the address is written.\n"
// clang-format on

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_H_
