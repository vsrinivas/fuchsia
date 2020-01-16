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

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_H_
