# HTTPSDate Timesource

HTTPSDate Timesource implements the [`fuchsia.time.external.PushSource`][time-fidl] protocol.
It retrieves time samples by calling an HTTPS URL specified in the component configuration and
pulling the time off the Date header in the response. The timesource relies on the underlying
TLS connection for server authentication.

[time-fidl]: /sdk/fidl/fuchsia.time.external

