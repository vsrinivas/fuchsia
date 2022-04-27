# input_pipeline > PointerMotionDisplayScaleHandler

Scales pointer motion, to make the physical size of pointer motion
invariant to display resolution. This allows products to use stock
mice and trackpads with high-DPI displays, without having to
implement pointer acceleration algorithms.

Notes:
* This handler is only suitable when the high-DPI display is used
  for smoother rendering of content, rather than for fitting more
  content onto a screen. In the latter case, scaling the pointer
  motion may make it difficult to hit the exact desired pixel.
* This handler deliberately deals only in `UnhandledInputEvent`s.
  That's because most handlers send an event to a FIDL peer
  before marking the event as handled. Scaling an event that has
  already been sent to FIDL peers might lead to inconsistencies,
  if another handler downstream from this handler also sends
  pointer events to FIDL peers.