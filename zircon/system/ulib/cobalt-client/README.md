# Cobalt Client

The cobalt client library provides a set of metrics than can be logged through cobalt.

Classes in this module are categorized in two groups:
 - Metrics: Represent data and provide methods to update them.
 - Collector: Provides mechanisms for collecting data and sending it to Coablt.
 - InMemoryLogger: Injectable logger into the Collector, which allows testing that metrics are correctly logged.

 ## Metric Options
 Cobalt metrics consist of:


 * metric_id: Unique identifier within the entire cobalt project. The metric id is associated with the metadata in the backend, and should match whatever definition is in the project config.

 * event_codes: Currently only 5 event codes per metric are supported by Cobalt. Event codes provides a more granular refinement for the metrics. (E.g.: Latency of reading a small file, and a big file). This event codes map to the enums defined in the backend, in your project definition.

 * component_name: A string that serves for further differentiating the metric being logged, for example a library provided metric that is logged from different user components, might set this so its able to identify the component logging such data.


These are reflected within the MetricOptions for Counter and HistogramOptions for Histograms. The HistogramOptions, provide mechanisms for defining the properties of the histogram suchs as Linear or Exponential, and the parameters used by the respective types.

## Persisting Data
The Collector does a best effort to persist the data into the Cobalt Service(the service might not be up). It is suggested a retry mechanic on a dispatcher thread to prevent blocking until success, which might never happpen(Cobalt Service never came up).


