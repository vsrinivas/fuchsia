# Mozart View Tiling Example

This directory contains a simple application which embeds any number of
views from other applications all tiled in a row. It also exposes a Presenter
service so that if one of embedded applications launches another application,
the launched application can present its View within the TileView.

The applications must implement the ViewProvider interface to be embedded.

## USAGE

Specify the urls of the views to embed initially as command-line arguments.

  $ file:///system/apps/launch file:///system/apps/tile_view <app1> <app2> ...

The following command-line options are also supported:

  Version mode for child views:

    --version=any   : composite most recent unblocked version of each child (default)
    --version=exact : composite only exact version of child specified during
                      layout (forces frame-level synchronization of resizing)

  Combinator mode for child views:

    --combinator=merge : use MERGE combinator (default)
    --combinator=prune : use PRUNE combinator
    --combinator=flash : use FALLBACK combinator with solid red color as
                         alternate content
    --combinator=dim   : use FALLBACK combinator with a dimmed layer containing the
                         most recent unblocked version of the child

  Orientation mode for child views:

    --horizontal : tile children horizontally
    --vertical   : tile children vertically
