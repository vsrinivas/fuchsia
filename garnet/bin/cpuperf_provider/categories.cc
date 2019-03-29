// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/categories.h"

#include <trace-engine/instrumentation.h>

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/lib/perfmon/events.h"

namespace cpuperf_provider {

void TraceConfig::Reset() {
  model_event_manager_ = nullptr;
  is_enabled_ = false;
  trace_os_ = false;
  trace_user_ = false;
  trace_pc_ = false;
  trace_last_branch_ = false;
  sample_rate_ = 0;
  timebase_event_ = PERFMON_EVENT_ID_NONE;
  selected_categories_.clear();
}

bool TraceConfig::ProcessCategories(const CategorySpec categories[],
                                    size_t num_categories,
                                    CategoryData* data) {
  for (size_t i = 0; i < num_categories; ++i) {
    const CategorySpec& cat = categories[i];
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
            case TraceOption::kLastBranch:
              trace_last_branch_ = true;
              break;
          }
          break;
        case CategoryGroup::kSample:
          if (data->have_sample_rate) {
            FXL_LOG(ERROR)
                << "Only one sampling mode at a time is currently supported";
            return false;
          }
          data->have_sample_rate = true;
          sample_rate_ = cat.value;
          break;
        case CategoryGroup::kFixedArch:
        case CategoryGroup::kFixedModel:
          selected_categories_.insert(&cat);
          data->have_data_to_collect = true;
          break;
        case CategoryGroup::kProgrammableArch:
        case CategoryGroup::kProgrammableModel:
          if (data->have_programmable_category) {
            // TODO(dje): Temporary limitation.
            FXL_LOG(ERROR) << "Only one programmable category at a time is "
                              "currently supported";
            return false;
          }
          data->have_programmable_category = true;
          data->have_data_to_collect = true;
          selected_categories_.insert(&cat);
          break;
      }
    }
  }

  return true;
}

bool TraceConfig::ProcessAllCategories() {
  // The default, if the user doesn't specify any categories, is that every
  // trace category is enabled. This doesn't work for us as the h/w doesn't
  // support enabling all events at once. And even when multiplexing support
  // is added it may not support multiplexing everything. So watch for the
  // default case, which we have to explicitly do as the only API we have is
  // trace_is_category_enabled(), and if present apply our own default.
  size_t num_enabled_categories = 0;
  for (size_t i = 0; i < kNumCommonCategories; ++i) {
    if (trace_is_category_enabled(kCommonCategories[i].name))
      ++num_enabled_categories;
  }
  for (size_t i = 0; i < kNumTargetCategories; ++i) {
    if (trace_is_category_enabled(kTargetCategories[i].name))
      ++num_enabled_categories;
  }
  bool is_default_case = num_enabled_categories ==
    (kNumCommonCategories + kNumTargetCategories);

  // Our default is to not trace anything: This is fairly specialized tracing
  // so we only provide it if the user explicitly requests it.
  if (is_default_case)
    return false;

  CategoryData category_data;

  if (!ProcessCategories(kCommonCategories, kNumCommonCategories,
                         &category_data)) {
    return false;
  }
  if (!ProcessCategories(kTargetCategories, kNumTargetCategories,
                         &category_data)) {
    return false;
  }

  // If neither OS,USER are specified, track both.
  if (!trace_os_ && !trace_user_) {
    trace_os_ = true;
    trace_user_ = true;
  }

  is_enabled_ = category_data.have_data_to_collect;
  return true;
}

bool TraceConfig::ProcessTimebase() {
  for (size_t i = 0; i < kNumTimebaseCategories; ++i) {
    const TimebaseSpec& cat = kTimebaseCategories[i];
    if (trace_is_category_enabled(cat.name)) {
      FXL_VLOG(1) << "Category " << cat.name << " enabled";
      if (timebase_event_ != PERFMON_EVENT_ID_NONE) {
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

void TraceConfig::Update(perfmon::ModelEventManager* model_event_manager) {
  Reset();

  model_event_manager_ = model_event_manager;

  if (ProcessAllCategories()) {
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
  if (trace_last_branch_ != old.trace_last_branch_)
    return true;
  if (sample_rate_ != old.sample_rate_)
    return true;
  if (timebase_event_ != old.timebase_event_)
    return true;
  if (selected_categories_ != old.selected_categories_)
    return true;
  return false;
}

bool TraceConfig::TranslateToDeviceConfig(perfmon_config_t* out_config) const {
  FXL_CHECK(model_event_manager_);

  perfmon_config_t* cfg = out_config;
  memset(cfg, 0, sizeof(*cfg));

  unsigned ctr = 0;

  // If a timebase is requested, it is the first event.
  if (timebase_event_ != PERFMON_EVENT_ID_NONE) {
    const perfmon::EventDetails* details;
    FXL_CHECK(model_event_manager_->EventIdToEventDetails(
                timebase_event_, &details));
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
        perfmon_event_id_t id = cat->events[i];
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
    flags |= PERFMON_CONFIG_FLAG_OS;
  if (trace_user_)
    flags |= PERFMON_CONFIG_FLAG_USER;
  if (timebase_event_ == PERFMON_EVENT_ID_NONE) {
    // These can only be set for events that are their own timebase.
    if (trace_pc_)
      flags |= PERFMON_CONFIG_FLAG_PC;
    if (trace_last_branch_)
      flags |= PERFMON_CONFIG_FLAG_LAST_BRANCH;
  }
  if (timebase_event_ != PERFMON_EVENT_ID_NONE)
    flags |= PERFMON_CONFIG_FLAG_TIMEBASE0;

  for (unsigned i = 0; i < num_used_events; ++i) {
    cfg->rate[i] = sample_rate_;
    cfg->flags[i] = flags;
  }

  if (timebase_event_ != PERFMON_EVENT_ID_NONE) {
    if (trace_pc_)
      cfg->flags[0] |= PERFMON_CONFIG_FLAG_PC;
    if (trace_last_branch_)
      cfg->flags[0] |= PERFMON_CONFIG_FLAG_LAST_BRANCH;
  }

  return true;
}

std::string TraceConfig::ToString() const {
  FXL_CHECK(model_event_manager_);

  std::string result;

  if (!is_enabled_)
    return "disabled";

  if (timebase_event_ != PERFMON_EVENT_ID_NONE) {
    const perfmon::EventDetails* details;
    FXL_CHECK(model_event_manager_->EventIdToEventDetails(timebase_event_, &details));
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
  if (trace_last_branch_)
    result += ",last_branch";

  for (const auto& cat : selected_categories_) {
    result += ",";
    result += cat->name;
  }

  return result;
}

}  // namespace cpuperf_provider
