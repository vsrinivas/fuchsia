// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(azani): Switch to a json, yaml or ascii proto files.

namespace cobalt {

// This must be kept in sync with registered_metrics.txt in the Cobalt repo.
const char* kMetricConfigText = R"(
#####################################################################
# customer: Fuchsia
# project:  Ledger
# Metric (1, 1, 1)
# Name: Daily rare event counts
# Description: Daily counts of several events that are expected to occur
#              rarely if ever.
# Parts: This metric has one part name "Event name"
# Notes: At least initially, we plan to use Basic RAPPOR with no privacy to
#        collect this metric. Each category will be one of the rare events.
######################################################################
element {
  customer_id: 1
  project_id: 1
  id: 1
  time_zone_policy: UTC
  parts {
    key: "Event name"
    value {
    }
  }
}

)";

// This must be kept in sync with registered_encodings.txt in the Cobalt repo.
const char* kEncodingConfigText = R"(
# Encoding: Basic RAPPOR with no random noise for Metric 1.
#####################################################################
# customer: Fuchsia
# project:  Ledger
# Encoding (1, 1, 1)
# Name: Basic RAPPOR for Daily Rare Event Counts
# Description: A Configuration of Basic RAPPOR with no privacy, with string
#              category names, and with one category for each rare event.
######################################################################
element {
  customer_id: 1
  project_id: 1
  id: 1
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    string_categories: {
      category: "Rare event 1"
      category: "Rare event 2"
      category: "Rare event 3"
    }
  }
}

)";

}  // namespace cobalt
