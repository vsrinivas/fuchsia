// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/categories.h"

#include <trace-engine/instrumentation.h>

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

namespace cpuperf_provider {

std::unique_ptr<TraceConfig> TraceConfig::Create(
    perfmon::ModelEventManager* model_event_manager,
    IsCategoryEnabledFunc is_category_enabled) {
  auto config = std::unique_ptr<TraceConfig>(
      new TraceConfig(model_event_manager, is_category_enabled));

  if (!config->ProcessAllCategories()) {
    return nullptr;
  }

  if (!config->ProcessTimebase()) {
    return nullptr;
  }

  return config;
}

TraceConfig::TraceConfig(perfmon::ModelEventManager* model_event_manager,
                         IsCategoryEnabledFunc is_category_enabled)
    : model_event_manager_(model_event_manager),
      is_category_enabled_(is_category_enabled) {
  FXL_DCHECK(model_event_manager);
}

bool TraceConfig::ProcessCategories(const CategorySpec categories[],
                                    size_t num_categories,
                                    CategoryData* data) {
  for (size_t i = 0; i < num_categories; ++i) {
    const CategorySpec& cat = categories[i];
    if (is_category_enabled_(cat.name)) {
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
    if (is_category_enabled_(kCommonCategories[i].name))
      ++num_enabled_categories;
  }
  for (size_t i = 0; i < kNumTargetCategories; ++i) {
    if (is_category_enabled_(kTargetCategories[i].name))
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
    if (is_category_enabled_(cat.name)) {
      FXL_VLOG(1) << "Category " << cat.name << " enabled";
      if (timebase_event_ != perfmon::kEventIdNone) {
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

bool TraceConfig::TranslateToDeviceConfig(perfmon::Config* out_config) const {
  perfmon::Config* cfg = out_config;
  cfg->Reset();

  uint32_t flags = 0;
  if (trace_os_) {
    flags |= perfmon::Config::kFlagOs;
  }
  if (trace_user_) {
    flags |= perfmon::Config::kFlagUser;
  }

  // These can only be set for events that are their own timebase.
  uint32_t pc_flags = 0;
  if (trace_pc_) {
    pc_flags |= perfmon::Config::kFlagPc;
  }
  if (trace_last_branch_) {
    pc_flags |= perfmon::Config::kFlagLastBranch;
  }

  uint32_t rate;

  if (timebase_event_ != perfmon::kEventIdNone) {
    const perfmon::EventDetails* details;
    FXL_CHECK(model_event_manager_->EventIdToEventDetails(
                timebase_event_, &details));
    FXL_VLOG(2) << fxl::StringPrintf("Using timebase %s", details->name);
    cfg->AddEvent(timebase_event_, sample_rate_,
                  flags | pc_flags | perfmon::Config::kFlagTimebase);
    rate = 0;
  } else {
    rate = sample_rate_;
    flags |= pc_flags;
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
      perfmon::EventId id = cat->events[i];
      perfmon::Config::Status status = cfg->AddEvent(id, rate, flags);
      if (status != perfmon::Config::Status::OK) {
        FXL_LOG(ERROR) << "Error processing event configuration: "
                       << perfmon::Config::StatusToString(status);
        return false;
      }
      FXL_VLOG(2) << fxl::StringPrintf("Adding %s event id %u to trace",
                                       group_name, id);
    }
  }

  return true;
}

std::string TraceConfig::ToString() const {
  std::string result;

  if (!is_enabled_)
    return "disabled";

  if (timebase_event_ != perfmon::kEventIdNone) {
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
