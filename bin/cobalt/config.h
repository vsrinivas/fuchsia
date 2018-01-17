// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_CONFIG_H_
#define GARNET_BIN_COBALT_CONFIG_H_

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

########################### Project 100: Ledger ###############################

#####################################################################
# Metric (1, 100, 1)
#
# DEPRECATED: Please use Metric (1, 100, 2) instead.
#
# Name:  Rare event occurrences.
# Description: Occurrences of several events that are expected to occur
#              rarely if ever.
# Parts: This metric has one part named "Event name"
# Notes: At least initially, we plan to use Basic RAPPOR with no privacy to
#        collect this metric. Each category will be one of the rare events.
######################################################################
element {
  customer_id: 1
  project_id: 100
  id: 1
  name: "Deprecated: Rare event occurrences"
  description: "Occurrences of several events that are expected to occur rarely if ever."
  time_zone_policy: UTC
  parts {
    key: "Event name"
    value {
      description: "Which rare event occurred?"
    }
  }
}

#####################################################################
# Metric (1, 100, 2)
# Name:  Rare event occurrences.
# Description: Occurrences of several events that are expected to occur
#              rarely if ever.
# Parts: This metric has one part named "event-index"
# Notes: At least initially, we plan to use Basic RAPPOR with no privacy to
#        collect this metric. The events are specified by a zero-based index.
#        The meaning of each index is not specified here. See the definition
#        of report config (1, 100, 2).
######################################################################
element {
  customer_id: 1
  project_id: 100
  id: 2
  name: "Rare event occurrences"
  description: "Occurrences of several events that are expected to occur rarely if ever."
  time_zone_policy: UTC
  parts {
    key: "event-index"
    value {
      description: "The index of the rare event that occurred."
      data_type: INDEX
    }
  }
}

##################### Project 101: Module Usage Tracking ######################

#####################################################################
# Metric (1, 101, 1)
# Name:  Module views
# Description: Tracks each incidence of viewing a module by its URL.
# Parts: This metric has one part named "url"
# Notes: At least initially, we plan to use Forculus with threshold set
#        to 2 to collect this. (Forculus doesn't support threshold=1.)
######################################################################
element {
  customer_id: 1
  project_id: 101
  id: 1
  name: "Module views"
  description: "Tracks each incidence of viewing a module by its URL."
  time_zone_policy: UTC
  parts {
    key: "url"
    value {
      description: "The URL of the module being launched."
    }
  }
}

#####################################################################
# Metric (1, 101, 2)
# Name:  Module pairs in story
# Description: Records pairs of modules (a, b) such that |a| was already part
#              of a story at the time that |b| was added to that story.
# Parts: This metric has two parts: one for the existing module and one
#
# Notes: We will use the no-op encoding with both parts.
######################################################################
element {
  customer_id: 1
  project_id: 101
  id: 2
  name: "Module pairs in story"
  description: "Records pairs of modules (a, b) such that |a| was already part of a story at the time that |b| was added to that story."
  time_zone_policy: UTC
  parts {
    key: "existing_module"
    value {
      description: "The URL of a module that was already in the story"
    }
  }
  parts {
    key: "added_module"
    value {
      description: "The URL of the module that was addded to a story"
    }
  }
}

######################### Project 102: System Metrics ##########################

#####################################################################
# Metric (1, 102, 1)
# Name: System Uptime
# Description: The system metrics daemon will log system uptime after 0, 1, 2, 4, 8... minutes.
# Parts: This metric has one part named "system_uptime_minutes".
#
# Notes: We will use the no-op encoding.
######################################################################
element {
  customer_id: 1
  project_id: 102
  id: 1
  name: "System Uptime"
  description: "The system metrics daemon will log system uptime after 0, 1, 2, 4, 8... minutes."
  time_zone_policy: UTC
  parts {
    key: "system_uptime_minutes"
    value {
      description: "How long since the system booted in minutes?"
      data_type: INT
    }
  }
}


################################################################################
#      ***  NOTICE: Below this line are testing-only projects. ***
#
#           These project must all use project IDs less than 100.
################################################################################

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
      description: "Which rare event occurred?"
    }
  }
}

#####################################################################
# Metric (1, 2, 2)
# Name:  Module views
# Description: Tracks each incidence of viewing a module by its URL.
# Parts: This metric has one part named "url"
# Notes: At least initially, we plan to use Forculus with threshold set
#        to 2 to collect this. (Forculus doesn't support threshold=1.)
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 2
  name: "Module views"
  description: "Tracks each incidence of viewing a module by its URL."
  time_zone_policy: UTC
  parts {
    key: "url"
    value {
      description: "The URL of the module being launched."
    }
  }
}

#####################################################################
# Metric (1, 2, 3)
# Name:  Rare event occurrences using indexes.
# Description: Occurrences of several events that are expected to occur
#              rarely if ever.
# Parts: This metric has one part named "event-index"
# Notes: At least initially, we plan to use Basic RAPPOR with no privacy to
#        collect this metric. Each category will be one of the rare events.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 3
  name: "Rare event occurrences"
  description: "Occurrences of several events that are expected to occur rarely if ever."
  time_zone_policy: UTC
  parts {
    key: "event-index"
    value {
      description: "The index of the rare event that occurred."
       data_type: INDEX
    }
  }
}

#####################################################################
# Metric (1, 2, 4)
# Name:  Module pairs in story
# Description: Records pairs of modules (a, b) such that |a| was already part
#              of a story at the time that |b| was added to that story.
# Parts: This metric has two parts: one for the existing module and one
#
# Notes: We will use the no-op encoding with both parts.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 4
  name: "Module pairs in story"
  description: "Records pairs of modules (a, b) such that |a| was already part of a story at the time that |b| was added to that story."
  time_zone_policy: UTC
  parts {
    key: "existing_module"
    value {
      description: "The URL of a module that was already in the story"
    }
  }
  parts {
    key: "added_module"
    value {
      description: "The URL of the module that was addded to a story"
    }
  }
}

#####################################################################
# Metric (1, 2, 5)
# Name:  Num_stars_in_sky
# Description: Records the number of stars in the sky each time we count.
# Parts: This metric has one part.
# Notes: We will use the no-op encoding.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 5
  name: "Num_stars_in_sky"
  description: "Records the number of stars in the sky each time we count."
  time_zone_policy: UTC
  parts {
    key: "num_stars"
    value {
      description: "The number of stars."
      data_type: INT
    }
  }
}

#####################################################################
# Metric (1, 2, 6)
# Name:  Average frob read time
# Description: Records the average time in seconds of all frob reads in the last hour.
# Parts: This metric has one part.
# Notes: We will use the no-op encoding.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 6
  name: "Average frob read time"
  description: "Records the average time in seconds of all frob reads in the last hour."
  time_zone_policy: UTC
  parts {
    key: "avg_time_s"
    value {
      description: "Average time in seconds."
      data_type: DOUBLE
    }
  }
}

)";

// This must be kept in sync with registered_encodings.txt in the Cobalt repo.
const char* kEncodingConfigText = R"(
#########################
# Customer 1 (Fuchsia)
########################

########################### Project 100: Ledger ###############################

#####################################################################
# EncodingConfig(1, 100, 1)
#
# DEPRECATED: Please use EncodingConfig (1, 100, 2) instead.
#
# Name:  Basic RAPPOR for Rare Event Occurrences
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
      category: "Ledger-startup"
      category: "Commits-received-out-of-order"
      category: "Commits-merged"
      category: "Merged-commits-merged"
      category: "Commits-received-out-of-order-not-recovered"
    }
  }
}

#####################################################################
# EncodingConfig(1, 100, 2)
# Name:  Basic RAPPOR for Rare Event Occurrences
# Description: A Configuration of Basic RAPPOR with no privacy, with 128
#              indexed categories.
######################################################################
element {
  customer_id: 1
  project_id: 100
  id: 2
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    indexed_categories: {
      num_categories: 128
    }
  }
}

##################### Project 101: Module Usage Tracking ######################

#####################################################################
# EncodingConfig(1, 101, 1)
# Name:  Forculus with minimal privacy
# Description:  We are using Forculus to collect arbitrary strings. We don't
#               need privacy at this time so we use a threshold of 2 and
#               an aggregation epoch of a month. This means that a string
#               will be decoded as long as it as sent by at least two differnt
#               devices in any given calendar month. At the time of this writing
#               we have not yet implemented persistent device identity so
#               every re-start of the Cobalt Client process on a Fuchsia
#               system counts as a new device.
######################################################################
element {
  customer_id: 1
  project_id: 101
  id: 1
  forculus {
    threshold: 2
    epoch_type: MONTH
  }
}

#####################################################################
# EncodingConfig(1, 101, 2)
# Name:  NoOp encoding
# Description:  Observations are sent unencoded.
######################################################################
element {
  customer_id: 1
  project_id: 101
  id: 2
  no_op_encoding {
  }
}

######################### Project 102: System Metrics ##########################

#####################################################################
# EncodingConfig(1, 102, 1)
# Name:  NoOp encoding
# Description:  Observations are sent unencoded.
######################################################################
element {
  customer_id: 1
  project_id: 102
  id: 1
  no_op_encoding {
  }
}

################################################################################
#      ***  NOTICE: Below this line are testing-only projects. ***
#
#           These project must all use project IDs less than 100.
################################################################################

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
      category: "Ledger-startup"
      category: "Commits-received-out-of-order"
      category: "Commits-merged"
      category: "Merged-commits-merged"
    }
  }
}

#####################################################################
# EncodingConfig(1, 2, 2)
# Name:  Forculus with minimal privacy
# Description:  We are using Forculus to collect arbitrary strings. We don't
#               need privacy at this time so we use a threshold of 2 and
#               an aggregation epoch of a month. This means that a string
#               will be decoded as long as it as sent by at least two differnt
#               devices in any given calendar month. At the time of this writing
#               we have not yet implemented persistent device identity so
#               every re-start of the Cobalt Client process on a Fuchsia
#               system counts as a new device.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 2
  forculus {
    threshold: 2
    epoch_type: MONTH
  }
}

#####################################################################
# EncodingConfig(1, 2, 3)
# Name:  Basic RAPPOR for Rare Event Occurrences
# Description: A Configuration of Basic RAPPOR with no privacy, with 128
#              indexed categories.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 3
  basic_rappor {
    prob_0_becomes_1: 0.0
    prob_1_stays_1: 1.0
    indexed_categories: {
      num_categories: 128
    }
  }
}

#####################################################################
# EncodingConfig(1, 2, 4)
# Name:  NoOp encoding
# Description:  Observations are sent unencoded.
######################################################################
element {
  customer_id: 1
  project_id: 2
  id: 4
  no_op_encoding {
  }
}

)";

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_CONFIG_H_
