# Dispatcher tests

These cover the client/server message dispatching layer:

 - client/server endpoints
 - client binding API
 - client internals
   * `fidl::internal::ResponseContext`
   * `fidl::internal::ClientBase`
 - server binding API
 - server internals
   * `fidl::Transaction`

It is preferable to keep tests self-contained and focused.
