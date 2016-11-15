# Mozart Input Manager

This directory contains a service which dispatches input events to views,
implemented as a ViewAssociate.

It doesn't make sense to run this application stand-alone since it
doesn't have any UI of its own to display.  Instead, use the Mozart
Presenter some other application to launch and embed the UI of some
other application using the view manager.
