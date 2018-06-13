# Hello BaseView

Usage example for ``scenic::BaseView`` class, which simplifies the creation of
components that can act as both parents and children in the Scenic view tree.

Includes a simple implementation of ``fuchsia.ui.policy.Presenter2`` which it
uses instead of connecting to ``root_presenter``; the topology is analogous to
how Peridot's ``device_runner`` connects the ``device_shell`` to
``root_presenter``.  Note: this requires connecting directly to Scenic, and
will not work if there is already a ``Compositor`` attached to the default
display.

To run:``run hello_base_view``
