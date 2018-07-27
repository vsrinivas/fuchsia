# A11y Touch Dispatcher

This service routes raw input events from one active Presentation to one
gesture detection client, and simulates inputs from the client back to
the presentation. This service is currently a standalone binary rather than a part
of a11y manager because the manager has no dependencies on the dispatcher. In the future,
it might make sense to implement this service in the a11y manager if the manager needs a
easy way of querying information from the dispatcher.
