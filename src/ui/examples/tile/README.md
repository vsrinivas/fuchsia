# Scenic View Tiling Example

This directory contains a simple application which embeds any number of
views from other applications all tiled in a row. It also exposes a Presenter
service so that if one of embedded applications launches another application,
the launched application can present its View within the TileView.

The applications must implement the ViewProvider interface to be embedded.

## USAGE

Specify the urls of the views to embed initially as command-line arguments.

```shell
$ present_view fuchsia-pkg://fuchsia.com/tile_view#meta/tile_view.cmx <app1> <app2> ...
```

The following command-line options are also supported:

  Orientation mode for child views:

    --horizontal : tile children horizontally (default)
    --vertical   : tile children vertically

Example:

```shell
$ present_view fuchsia-pkg://fuchsia.com/tile_view#meta/tile_view.cmx \
    --horizontal \
    fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx \
    fuchsia-pkg://fuchsia.com/shapes_view#meta/shapes_view.cmx
```
