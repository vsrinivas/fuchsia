// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains copies of the contents of the config registration
// files that are located in the Cobalt repo. The Cobalt client and
// server need to share common configuration files and this constitutes
// the client's copy.
// TODO(azani) Find a better solution. We should be able to copy the
// contents of the files at build time. Also we need to switch away
// from ascii proto to something like json or yaml.

namespace cobalt {

// This must be kept in sync with registered_metrics.txt in the Cobalt repo.
const char* kMetricConfigText = R"(
#########################
# Customer 1 (Fuchsia)
########################

## Project 100: Ledger

#####################################################################
# Metric (1, 100, 1)
# Name:  Daily rare event counts
# Description: Daily counts of several events that are expected to occur
#              rarely if ever.
# Parts: This metric has one part name "Event name"
# Notes: At least initially, we plan to use Basic RAPPOR with no privacy to
#        collect this metric. Each category will be one of the rare events.
######################################################################
element {
  customer_id: 1
  project_id: 100
  id: 1
  name: "Daily rare event counts"
  description: "Daily counts of several events that are expected to occur rarely if ever."
  time_zone_policy: UTC
  parts {
    key: "Event name"
    value {
      description: "Which rare event occured?"
    }
  }
}

#####################################################################
#        ***  Below this line are testing projects. ***
#####################################################################

## Project 1: End-to-End test

#### Metric (1, 1, 1)
element {
  customer_id: 1
  project_id: 1
  id: 1
  name: "Fuchsia Popular URLs"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "url"
    value {
      description: "A URL."
      data_type: STRING
    }
  }
}

#### Metric (1, 1, 2)
element {
  customer_id: 1
  project_id: 1
  id: 2
  name: "Fuschsia Usage by Hour"
  description: "This is a fictional metric used for the development of Cobalt."
  time_zone_policy: LOCAL
  parts {
    key: "hour"
    value {
      description: "An integer from 0 to 23 representing the hour of the day."
      data_type: INT
    }
  }
}

## Project 2: Fuchsia Test App

#####################################################################
# Metric (1, 2, 1)
# Name:  Daily rare event counts
# Description: Daily counts of several events that are expected to occur
#              rarely if ever.
# Parts: This metric has one part name "Event name"
# Notes: At least initially, we plan to use Basic RAPPOR with no privacy to
#        collect this metric. Each category will be one of the rare events.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 1
  name: "Daily rare event counts"
  description: "Daily counts of several events that are expected to occur rarely if ever."
  time_zone_policy: UTC
  parts {
    key: "Event name"
    value {
      description: "Which rare event occured?"
    }
  }
}
)";

// This must be kept in sync with registered_encodings.txt in the Cobalt repo.
const char* kEncodingConfigText = R"(
#########################
# Customer 1 (Fuchsia)
########################

## Project 100: Ledger

#####################################################################
# EncodingConfig(1, 100, 1)
# Name:  Basic RAPPOR for Daily Rare Event Counts
# Description: A Configuration of Basic RAPPOR with no privacy, with string
#              category names, and with one category for each rare event.
######################################################################
element {
  customer_id: 1
  project_id: 100
  id: 1
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    string_categories: {
      category: "Rare-event-1"
      category: "Rare-event-2"
      category: "Rare-event-3"
    }
  }
}


#####################################################################
#        ***  Below this line are testing projects. ***
#####################################################################

## Project 1: End-to-End test

#### EncodingConfig(1, 1, 1)
element {
  customer_id: 1
  project_id: 1
  id: 1
  forculus {
    threshold: 20
    epoch_type: DAY
  }
}

#### EncodingConfig(1, 1, 2)
element {
  customer_id: 1
  project_id: 1
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.1
    prob_1_stays_1: 0.9
    int_range_categories: {
      first: 0
      last:  23
    }
  }
}

## Project 2: Fuchsia Test App

#####################################################################
# EncodingConfig(1, 2, 1)
# Name:  Basic RAPPOR for Daily Rare Event Counts
# Description: A Configuration of Basic RAPPOR with no privacy, with string
#              category names, and with one category for each rare event.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 1
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    string_categories: {
      category: "Rare-event-1"
      category: "Rare-event-2"
      category: "Rare-event-3"
    }
  }
}
)";

}  // namespace cobalt
