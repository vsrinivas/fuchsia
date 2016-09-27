# Mozart View Tiling Example

This directory contains a simple application which embeds any number of
views from other applications all tiled in a row.

The applications must implement the ViewProvider interface and register
their Views with the ViewManager for this to work.

## USAGE

Specify the urls of the views to embed as a comma-delimited query string.

  $ application_manager "mojo:launcher mojo:tile_view?views=<app1>[,<app2>[,...]]"
  $ application_manager "mojo:launcher mojo:tile_view?views=mojo:spinning_cube_view,mojo:noodles_view"

The query string may also encode tiling options by appending parameters to
the end of the query string.

  Version mode for child views:

    &vm=any   : composite most recent unblocked version of each child (default)
    &vm=exact : composite only exact version of child specified during
                layout (forces frame-level synchronization of resizing)

  Combinator mode for child views:

    &cm=merge : use MERGE combinator (default)
    &cm=prune : use PRUNE combinator
    &cm=flash : use FALLBACK combinator with solid red color as
                alternate content
    &cm=dim   : use FALLBACK combinator with a dimmed layer containing the
                most recent unblocked version of the child

  Orientation mode for child views:

    &o=h : tile children horizontally
    &o=v : tile children vertically
