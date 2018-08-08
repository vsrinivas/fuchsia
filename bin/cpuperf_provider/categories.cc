// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/categories.h"

#include <trace-engine/instrumentation.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/cpuperf/events.h"

namespace cpuperf_provider {

enum EventId {
#define DEF_FIXED_EVENT(symbol, id, regnum, flags, name, description) \
  symbol = CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_FIXED, id),
#define DEF_ARCH_EVENT(symbol, id, ebx_bit, event, umask, flags, name, \
                       description)                                    \
  symbol = CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_ARCH, id),
#include <zircon/device/cpu-trace/intel-pm-events.inc>

#define DEF_SKL_EVENT(symbol, id, event, umask, flags, name, description) \
  symbol = CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_MODEL, id),
#include <zircon/device/cpu-trace/skylake-pm-events.inc>

#define DEF_MISC_SKL_EVENT(symbol, id, offset, size, flags, name, description) \
  symbol = CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_MISC, id),
#include <zircon/device/cpu-trace/skylake-misc-events.inc>
};

#define DEF_FIXED_CATEGORY(symbol, name, events...) \
  static const cpuperf_event_id_t symbol##_events[] = {events};
#define DEF_ARCH_CATEGORY(symbol, name, events...) \
  static const cpuperf_event_id_t symbol##_events[] = {events};
#include "intel-pm-categories.inc"

#define DEF_SKL_CATEGORY(symbol, name, events...) \
  static const cpuperf_event_id_t symbol##_events[] = {events};
#include "skylake-pm-categories.inc"

#define DEF_MISC_SKL_CATEGORY(symbol, name, events...) \
  static const cpuperf_event_id_t symbol##_events[] = {events};
#include "skylake-misc-categories.inc"

static const CategorySpec kCategories[] = {
    // Options
    {"cpu:os", CategoryGroup::kOption,
     static_cast<CategoryValue>(TraceOption::kOs), 0, nullptr},
    {"cpu:user", CategoryGroup::kOption,
     static_cast<CategoryValue>(TraceOption::kUser), 0, nullptr},
    {"cpu:pc", CategoryGroup::kOption,
     static_cast<CategoryValue>(TraceOption::kPc), 0, nullptr},

// Sampling rates.
// Only one of the following is allowed.
#define DEF_SAMPLE(name, value) \
  { "cpu:" name, CategoryGroup::kSample, value, 0, nullptr }
    DEF_SAMPLE("tally", 0),
    DEF_SAMPLE("sample:100", 100),
    DEF_SAMPLE("sample:500", 500),
    DEF_SAMPLE("sample:1000", 1000),
    DEF_SAMPLE("sample:5000", 5000),
    DEF_SAMPLE("sample:10000", 10000),
    DEF_SAMPLE("sample:50000", 50000),
    DEF_SAMPLE("sample:100000", 100000),
    DEF_SAMPLE("sample:500000", 500000),
    DEF_SAMPLE("sample:1000000", 1000000),
#undef DEF_SAMPLE

// TODO(dje): Reorganize fixed,arch,skl(model),misc vs
// fixed/programmable+arch/model.

// Fixed events.
#define DEF_FIXED_CATEGORY(symbol, name, events...)                     \
  {"cpu:" name, CategoryGroup::kFixedArch, 0, countof(symbol##_events), \
   &symbol##_events[0]},
#include "intel-pm-categories.inc"

// Architecturally specified programmable events.
#define DEF_ARCH_CATEGORY(symbol, name, events...)                             \
  {"cpu:" name, CategoryGroup::kProgrammableArch, 0, countof(symbol##_events), \
   &symbol##_events[0]},
#include "intel-pm-categories.inc"

// Model-specific misc events
#define DEF_MISC_SKL_CATEGORY(symbol, name, events...)                   \
  {"cpu:" name, CategoryGroup::kFixedModel, 0, countof(symbol##_events), \
   &symbol##_events[0]},
#include "skylake-misc-categories.inc"

// Model-specific programmable events.
#define DEF_SKL_CATEGORY(symbol, name, events...)     \
  {"cpu:" name, CategoryGroup::kProgrammableModel, 0, \
   countof(symbol##_events), &symbol##_events[0]},
#include "skylake-pm-categories.inc"
};

static const TimebaseSpec kTimebaseCategories[] = {
#define DEF_TIMEBASE_CATEGORY(symbol, name, event) {"cpu:" name, event},
#include "intel-timebase-categories.inc"
};

void TraceConfig::Reset() {
  is_enabled_ = false;
  trace_os_ = false;
  trace_user_ = false;
  trace_pc_ = false;
  sample_rate_ = 0;
  timebase_event_ = CPUPERF_EVENT_ID_NONE;
  selected_categories_.clear();
}

bool TraceConfig::ProcessCategories() {
  // The default, if the user doesn't specify any categories, is that every
  // trace category is enabled. This doesn't work for us as the h/w doesn't
  // support enabling all events at once. And event when multiplexing support
  // is added it may not support multiplexing everything. So watch for the
  // default case, which we have to explicitly do as the only API we have is
  // trace_is_category_enabled(), and if present apply our own default.
  size_t num_enabled_categories = 0;
  for (const auto& cat : kCategories) {
    if (trace_is_category_enabled(cat.name))
      ++num_enabled_categories;
  }
  bool is_default_case = num_enabled_categories == countof(kCategories);

  // Our default is to not trace anything: This is fairly specialized tracing
  // so we only provide it if the user explicitly requests it.
  if (is_default_case)
    return false;

  bool have_something = false;
  bool have_sample_rate = false;
  bool have_programmable_category = false;

  for (const auto& cat : kCategories) {
    if (trace_is_category_enabled(cat.name)) {
      FXL_VLOG(1) << "Category " << cat.name << " enabled";
      switch (cat.group) {
        case CategoryGroup::kOption:
          switch (static_cast<TraceOption>(cat.value)) {
            case TraceOption::kOs:
              trace_os_ = true;
              break;
            case TraceOption::kUser:
              trace_user_ = true;
              break;
            case TraceOption::kPc:
              trace_pc_ = true;
              break;
          }
          break;
        case CategoryGroup::kSample:
          if (have_sample_rate) {
            FXL_LOG(ERROR)
                << "Only one sampling mode at a time is currenty supported";
            return false;
          }
          have_sample_rate = true;
          sample_rate_ = cat.value;
          break;
        case CategoryGroup::kFixedArch:
        case CategoryGroup::kFixedModel:
          selected_categories_.insert(&cat);
          have_something = true;
          break;
        case CategoryGroup::kProgrammableArch:
        case CategoryGroup::kProgrammableModel:
          if (have_programmable_category) {
            // TODO(dje): Temporary limitation.
            FXL_LOG(ERROR) << "Only one programmable category at a time is "
                              "currenty supported";
            return false;
          }
          have_programmable_category = true;
          have_something = true;
          selected_categories_.insert(&cat);
          break;
      }
    }
  }

  // If neither OS,USER are specified, track both.
  if (!trace_os_ && !trace_user_) {
    trace_os_ = true;
    trace_user_ = true;
  }

  is_enabled_ = have_something;
  return true;
}

bool TraceConfig::ProcessTimebase() {
  for (const auto& cat : kTimebaseCategories) {
    if (trace_is_category_enabled(cat.name)) {
      FXL_VLOG(1) << "Category " << cat.name << " enabled";
      if (timebase_event_ != CPUPERF_EVENT_ID_NONE) {
        FXL_LOG(ERROR) << "Timebase already specified";
        return false;
      }
      if (sample_rate_ == 0) {
        FXL_LOG(ERROR) << "Timebase cannot be used in tally mode";
        return false;
      }
      timebase_event_ = cat.event;
    }
  }

  return true;
}

void TraceConfig::Update() {
  Reset();

  if (ProcessCategories()) {
    if (ProcessTimebase()) {
      return;
    }
  }

  // Some error occurred while parsing the selected categories.
  Reset();
}

bool TraceConfig::Changed(const TraceConfig& old) const {
  if (is_enabled_ != old.is_enabled_)
    return true;
  if (trace_os_ != old.trace_os_)
    return true;
  if (trace_user_ != old.trace_user_)
    return true;
  if (trace_pc_ != old.trace_pc_)
    return true;
  if (sample_rate_ != old.sample_rate_)
    return true;
  if (timebase_event_ != old.timebase_event_)
    return true;
  if (selected_categories_ != old.selected_categories_)
    return true;
  return false;
}

bool TraceConfig::TranslateToDeviceConfig(cpuperf_config_t* out_config) const {
  cpuperf_config_t* cfg = out_config;
  memset(cfg, 0, sizeof(*cfg));

  unsigned ctr = 0;

  // If a timebase is requested, it is the first event.
  if (timebase_event_ != CPUPERF_EVENT_ID_NONE) {
    const cpuperf::EventDetails* details;
    FXL_CHECK(cpuperf::EventIdToEventDetails(timebase_event_, &details));
    FXL_VLOG(2) << fxl::StringPrintf("Using timebase %s", details->name);
    cfg->events[ctr++] = timebase_event_;
  }

  for (const auto& cat : selected_categories_) {
    const char* group_name;
    switch (cat->group) {
      case CategoryGroup::kFixedArch:
        group_name = "fixed-arch";
        break;
      case CategoryGroup::kFixedModel:
        group_name = "fixed-model";
        break;
      case CategoryGroup::kProgrammableArch:
        group_name = "programmable-arch";
        break;
      case CategoryGroup::kProgrammableModel:
        group_name = "programmable-model";
        break;
      default:
        FXL_NOTREACHED();
    }
    for (size_t i = 0; i < cat->count; ++i) {
      if (ctr < countof(cfg->events)) {
        cpuperf_event_id_t id = cat->events[i];
        FXL_VLOG(2) << fxl::StringPrintf("Adding %s event id %u to trace",
                                         group_name, id);
        cfg->events[ctr++] = id;
      } else {
        FXL_LOG(ERROR) << "Maximum number of events exceeded";
        return false;
      }
    }
  }
  unsigned num_used_events = ctr;

  uint32_t flags = 0;
  if (trace_os_)
    flags |= CPUPERF_CONFIG_FLAG_OS;
  if (trace_user_)
    flags |= CPUPERF_CONFIG_FLAG_USER;
  if (timebase_event_ == CPUPERF_EVENT_ID_NONE) {
    // These can only be set for events that are their own timebase.
    if (trace_pc_)
      flags |= CPUPERF_CONFIG_FLAG_PC;
  }
  if (timebase_event_ != CPUPERF_EVENT_ID_NONE)
    flags |= CPUPERF_CONFIG_FLAG_TIMEBASE0;

  for (unsigned i = 0; i < num_used_events; ++i) {
    cfg->rate[i] = sample_rate_;
    cfg->flags[i] = flags;
  }

  if (timebase_event_ != CPUPERF_EVENT_ID_NONE) {
    if (trace_pc_)
      cfg->flags[0] |= CPUPERF_CONFIG_FLAG_PC;
  }

  return true;
}

std::string TraceConfig::ToString() const {
  std::string result;

  if (!is_enabled_)
    return "disabled";

  if (timebase_event_ != CPUPERF_EVENT_ID_NONE) {
    const cpuperf::EventDetails* details;
    FXL_CHECK(cpuperf::EventIdToEventDetails(timebase_event_, &details));
    result +=
        fxl::StringPrintf("Timebase 0x%x(%s)", timebase_event_, details->name);
  }

  if (sample_rate_ > 0) {
    result += fxl::StringPrintf("@%u", sample_rate_);
  } else {
    result += "tally";
  }

  if (trace_os_)
    result += ",os";
  if (trace_user_)
    result += ",user";
  if (trace_pc_)
    result += ",pc";

  for (const auto& cat : selected_categories_) {
    result += ",";
    result += cat->name;
  }

  return result;
}

}  // namespace cpuperf_provider
