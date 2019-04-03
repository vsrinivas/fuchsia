# fuchsia-component

fuchsia-component is a crate designed to allow components to launch, connect to,
and provide services to other components.

Some of the functionality provided by fuchsia-component was formerly present in the
fuchsia-app crate, which is now deprecated.

fuchsia-component can do everything fuchsia-app could, but includes a new
name, a more ergonomic API, and a more flexible directory implementation that
allows more precisely managing where outgoing services are located within the
outgoing directory hierarchy. fuchsia-app exposed all services at /\* and
/public/\*, while fuchsia-component allows developers to choose a precise
location for each service. This is critical for developers who wish to expose
internal-only debugging services such as the Inspect API.
