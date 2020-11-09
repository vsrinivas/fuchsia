# Fuchsia Metrics

This library contains interfaces that allow clients to log events that are
associated with metrics. These events are collected and later analyzed.

## Overview

Metrics are organized under a Project, which are associated with a Customer.
Each of these objects has an integer ID and those IDs are used as parameters
in the methods in this file. Metrics can also have one or more dimensions
associated with them, which are then passed as a vector of event codes when
logging the event.

## Implementation

The default implementation of this service in Fuchsia is Cobalt, which is an
end-to-end solution to log, collect and analyze metrics. The two main pillars
of Cobalt are protecting user privacy and providing high-quality, aggregate
metrics to serve system and component software developers' needs.

To use Cobalt, you must have a Project and one or more Metrics registered
with the Cobalt registration system. You must also register one or more
Reports in order to see the results of your logging aggregated over
all Fuchsia devices. Registration of Projects, Metrics and Reports consists
of entries in the YAML files in
[this repository](https://fuchsia.googlesource.com/cobalt-registry).
In a Fuchsia checkout, this is mapped to
[//third_party/cobalt_config](/third_party/cobalt_config).

## Usage

First you use MetricEventLoggerFactory to get a MetricEventLogger for your
project. Then you log Events as they occur, using the Log*() methods on it.
In the Cobalt FIDL service implementation, Events are accumulated and
periodically Observations, derived from the logged Events, are sent to the
Cobalt server, where they are used to generate Reports.

## Protocols

* **MetricEventLoggerFactory**: a factory that is used to create a
  MetricEventLogger for a specific project. Takes a specification identifying a
  project to log events for. For Cobalt, the specification's IDs are specified
  in Cobalt's registry repository.

* **MetricEventLogger**: a logger for events that are associated with one
  project's metrics. For Cobalt, the `metric_id` must be one of the metrics
  declared in Cobalt's registry for the project associated with this logger.
  And each event code in `event_codes` is a key from the `event_codes` map of
  the corresponding MetricDimension for the metric in Cobalt's registry.
  
  In addition, for Cobalt, the following applies to each of the
  MetricEventLogger methods:
  
  * LogOccurrence: `metric_id` must correspond to a metric of type OCCURRENCE.
    
  * LogInteger: `metric_id` must correspond to a metric of type INTEGER.
    
  * LogIntegerHistogram: `metric_id` must correspond to a metric of type
    INTEGER_HISTOGRAM.
    
  * LogString: `metric_id` must correspond to a metric of type STRING.
    
  * LogCustomEvent: `metric_id` must correspond to a metric of type CUSTOM, and
    the `event_values` must have a dimension name and type of value that matches
    the field names and their types for the metric as declared in Cobalt's
    registry.
