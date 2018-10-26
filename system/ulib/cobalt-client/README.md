# Cobalt Client

The cobalt client library provides a set of metrics than can be logged through cobalt.

Classes in this module are categorized in two groups:
 - Metrics: Represent data and provide methods to update them.
 - Collector: Owns the data, and is in charge of flushing data to services.

##  Collector Options
```c++
cobalt_client::CollectorOptions MakeOptions() {
    cobalt_client::CollectorOptions options;


    // The collector preallocates space for the metrics to be collected, so when
    // instantiated we set the number of each metric type we want to store.
    options.max_histograms = 4;
    options.max_counters = 4;

    // The collector also needs to define how it should behave when attempting
    // to reach a remote service(IPC/FIDL), such as cobalt. The response
    // deadline, is for setting up the remote logger in the remote service.
    // This is the time it will block when checking if the service already
    // replied.
    options.response_deadline = zx::nsec(0);

    // Just as before, but this is when we write the remote logger request, we
    // immediately wait for this amount of time.
    options.initial_response_deadline = zx::nsec(100);

    // Cobalt service requires the serialized config for setting up the service.
    // Since we cannot assume that the filesystem is ready by the time we are
    // instantiating the collector (e.g. a filesystem collecting metrics)
    // we defer it by passing a fbl::Function.
    options.load_config = [] (zx::vmo* vmo, size_t* total_bytes) -> bool {
        // We could speed this up by a fidl call to obtain a VMO to a file.
        zx::vmo::create(....., vmo);
        *total_bytes = read(.....);
        // We check if we can read.
        return vmo->write(buffer,0, total_bytes) == ZX_OK;
    };
    return fbl::move(options);
}
```

## Collector
```c++
class Delorean {
private:
    cobalt_client::Collector collector_ = Collector::Debug(MakeOptions());
}

...
// Attempt to send the metrics to cobalt.
collector_.Flush();
```
Collector can only be instantiated by users through the public factory methods,
which describe the target release stage(Debug, Fishfood, Dogfood, General
Availability).

The collector is in charge of flushing or sending the metrics to the cobalt
service. Because we make no assumptions on the existence of the service, or the
availability of the service, the following decisions were made:

- Best effort: Try to send the metrics to cobalt process. If we fail, undo the
 flush, and try again later.
- Reconnect: We assume the channel can be closed at any time, which is why we
 re-stablish the connection when this happens and we attempt to flush.
- Delegate when to flush: The user determines when is the best momento to send
 the metrics to FIDL.


The Collector also owns the storage for the metrics, so any metric that was
instantiated from a Collector should be used after a collector's destruction.
Usually the collector and the metrics should be members of the same class
(either directly or indirectly).

## Counter
```c++
enum TimeTravelDirection : uint32_t {
    kUnknown = 0,
    kPast = 1,
    kFuture  = 2,
};

MetricOptions options;
options.Remote();
options.metric_id = kTimeTravelYears;
options.event_code = kPast;
options.component = "Great Scott!";

cobalt_client::Counter years_traveled = collector_.AddCounter(options);
if (total_years == 1) {
    // By default Increment increments by 1 unit.
    years_travelled.Increment();
} else {
    // Or you can increment a single time.
    years_travelled.Increment(num_events);
}
```

Counter is a shallow proxy, which is why it can be copied or moved, with no
effect what so ever.

## Histogram
```c++
HistogramOptions options = HistogramOptions::Exponential(
    /*bucket_count=*/10, /*base=*/2, /*scalar=*/1, /*offset=*/2);
options.Remote();
options.metric_id = kTimeTravelYears;
options.event_code = kPast;
options.component = "Great Scott!";

cobalt_client::Histogram years_per_travel = collector_.AddHistogram(options);
uint64_t current_year = 1955;
uint64_t original_year = 1985;
uint64_t years_traveled = abs(current_year - original_year);
uint64_t nums_of_travels = 1;
if (years_traveled == 1) {
    years_per_travel.Add(years_traveled);
} else {
    years_per_travel.Add(years_traveled, nums_of_travels);
}
```
Histogram is a shallow proxy for the data owned by the collector, and provides
methods for adding observed values.  The Options must mimick those set up in
your cobalt project configuration.


## Remote Vs Local values

Eventually the library will provide the ability to instatiate local version of
the metrics for the Instrospection API.  So GetRemoteCount methods, reference
the values that have not yet been sent to Cobalt.
