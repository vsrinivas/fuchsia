# Hello BaseView

Usage example for ``scenic::BaseView`` class, which simplifies the creation of
components that can act as both parents and children in the Scenic view tree.

Includes a simple implementation of ``fuchsia.ui.policy.Presenter2`` which it
uses instead of connecting to ``root_presenter``; the topology is analogous to
how Peridot's ``device_runner`` connects the ``device_shell`` to
``root_presenter``.  Note: this requires connecting directly to Scenic, and
will not work if there is already a ``Compositor`` attached to the default
display.

Usage:

Using the root presenter: ``run hello_base_view --use_root_presenter``
Using the example presenter: ``run hello_base_view --use_example_presenter``
Using the view provider service, ``set_root_view hello_base_view`` (not supported yet)

If using example_presenter, it will be necessary to kill any instances of scenic
and root_presenter (use ``killall``).
