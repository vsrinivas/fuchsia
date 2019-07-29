# Views, Bounds, and Clipping

- [Introduction](#introduction)

- [Concepts](#concepts)
  - [Setting View Bounds](#setting-view-bounds)
    - [Bound Extent and Insets](#bound-extent-and-insets)
    - [Example](#example1)
    - [Example 2](#example-2)
  - [Coordinate System](#coordinate-system)
    - [Example](#example3)
  - [Centering Geometry](#centering-geometry)
  - [Debug Wireframe Rendering](#debug-wireframe-rendering)
  - [Ray Casting and Hit Testing](#ray-casting-and-hit-testing)
    - [Rules](#rules)
    - [Edge Cases](#edge-cases)
    - [Pixel Offsets](#pixel-offsets)
      - [Example](#example4)

# Introduction

This is a guide that explains how view bounds and clipping work in Scenic. This guide explains how to set view bounds, how to interpret what the commands are doing, and the effects that the view bounds have on existing Scenic subsystems.

# Concepts

## Setting View Bounds

An embedder must create a pair of tokens for a view and view holder, and must also allocate space within its view for the embedded view holder to be laid out. This is done by setting the bounds on the view holder of the embedded view. To set the view bounds on a view, you have to call `SetViewProperties` on its respective ViewHolder. You can call `SetViewProperties` either before or after the view itself is created and linked to the ViewHolder, so you do not have to worry about the order in which you do your setup. The bounds themselves are set by specifying their minimum and maximum points (xyz) in 3D space.

### Bound Extent and Insets

There are four values needed to set a view's bounds properly, `bounds_min`, `bounds_max`, `inset_min` and `inset_max`. The minimum and maximum bounds represent the minimum and maximum coordinate points of an axis-aligned bounding box. The minimum and maximum insets specify the distances between the view’s bounding box and that of its parent. So the final extent of a view's bounds can be defined with the following formula:

```cpp
{ bounds_min + inset_min, bounds_max - inset_max}
```

### Example <a name="example1"></a>

```cpp

// Create a pair of tokens to register a view and view holder in
// the scene graph.
auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

// Create the actual view and view holder.
scenic::View view(session, std::move(view_token), "View");
scenic::ViewHolder view_holder(session, std::move(view_holder_token),
                               “ViewHolder");

// Set the bounding box dimensions and set them on the view holder.
const float bounds_min[3] = {0.f, 0.f, -200.f};
const float bounds_max[3] = {500, 500, 0};
const float inset_min[3] = {20, 30, 0};
const float inset_max[3] = {20, 30, 0};
view_holder.SetViewProperties(bounds_min, bounds_max,
                          inset_min, inset_max);
```

The above code creates a View and ViewHolder pair whose bounds start at (20,30,-200) and extend out to (480,470,0). The bounds themselves are always axis-aligned.

The above version of `SetViewProperties` requires you to supply each parameter individually, but you can also call another version of the function which takes in a `ViewProperties` struct, instead.

### Example 2

```cpp

// Create a 'ViewProperties' struct and fill it with the bounding
// box dimensions.
fuchsia::ui::gfx::ViewProperties properties;
properties.bounding_box.min =
            fuchsia::ui::gfx::vec3{.x = 0, .y = 0, .z = -200};
properties.bounding_box.max =
            fuchsia::ui::gfx::vec3{.x = 500, .y = 500, .z = 0);

// Set the view properties on the view holder.
view_holder.SetViewProperties(std::move(properties));
```

## Coordinate System

View bounds are specified in local coordinates, and their world-space position is determined by the global transform of the view node.

### Example <a name="example3"></a>

```cpp
// Create an entity node and translate it by (100,100,200).
scenic::EntityNode transform_node(session);
transform_node.SetTranslation(100, 100, 200);

// Add the transform node as a child of the root node.
root_node.AddChild(transform_node);

// Create a view and view-holder token pair.
auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
scenic::View view(session, std::move(view_token), "View");
scenic::ViewHolder view_holder(session, std::move(view_holder_token),
                               "ViewHolder");

// Attach the view holder as a child of the transform node.
transform_node.Attach(view_holder);

// Set view bounds.
const float bounds_min[3] = {0.f, 0.f, 0.f};
const float bounds_max[3] = {500, 500, 200};
const float inset_min[3] = {0, 0, 0};
const float inset_max[3] = {0, 0, 0};
view_holder.SetViewProperties(bounds_min, bounds_max,
                           inset_min, inset_max);
```

In the above code, the view bounds in local space have a min and max value of (0,0,0) and (500,500,200), but since the parent node is translated by (100,100,200) the view bounds in world space will actually have a world space bounds min and max of (100,100,200) and (600,600,400) respectively. However, the view itself doesn’t see these world-space bounds, and only deals with its bounds in its own local space.

## Centering Geometry

The center of mass for a piece of geometry such as a `RoundedRectangle` is its center, whereas for a view, the center of mass for its bounds is its minimum coordinate. This means that if a view and a rounded-rectangle that is a child of that view both have the same translation, the center of the rounded-rectangle will render at the minimum-coordinate of the view’s bounds. To fix this, apply another translation on the shape node to move it to the center of the view’s bounds.

![Centering Geometry Diagram](meta/scenic_centering_geometry.png)

## Debug Wireframe Rendering

To help with debugging view bounds, you can render the edges of the bounds in wire-frame mode to see where exactly your view is located in world space. This functionality can be applied per-view using a Scenic command:

```cpp
// This command turns on wireframe rendering of the specified
// view, which can aid in debugging.
struct SetEnableDebugViewBoundsCmd {
    uint32 view_id;
    bool display_bounds;
};
```

This command takes in a `view id`, and a `bool` to toggle whether or not the view bounds should be displayed. The default display color is white, but you can choose different colors by running the `SetViewHolderBoundsColorCmd` on the specified view holder:

```cpp
// This command determines the color to be set on a view holder’s debug
// wireframe bounding box.
struct SetViewHolderBoundsColorCmd {
    uint32 view_holder_id;
    ColorRgbValue color;
};
```

## Ray Casting and Hit Testing

When performing hit tests, Scenic runs tests against the bounds of a `ViewNode` before determining whether the ray should continue checking children of that node. If you forget to set the bounds for a view, any geometry that exists as a child of that view cannot be hit. This is because the bounds would be null and therefore infinitely small, which also means that there would be no geometry rendered to the screen.

### Rules

These are the rules for ray casting:

* If a ray completely misses a view’s bounding box, nothing that is a child of that   view will be hit.

* If a ray does intersect a bounding box, only geometry that exists within the range   of the ray’s entrance and exit from the bounding box will be considered for a hit. For example, clipped geometry cannot be hit.

In debug mode, a null bounding box will trigger an FXL_DCHECK in the escher::BoundingBox class stating that the bounding box dimensions need to be greater than or equal to 2.

### Edge Cases

 Situations where a ray is perpendicular to a side of a bounding box and just grazes its edge will not count as a hit. Since the six planes that constitute the bounding box are themselves the clip planes, it follows that anything that is directly on a clip plane would also get clipped.

### Pixel Offsets

When issuing input commands in screen space, pixel values are jittered by (0.5, 0.5) so that commands are issued from the center of the pixel and not the top-left corner. This is important to take into account when testing ray-hit tests with bounding boxes, as it will affect the ray origins in world space after they have been transformed, and thus whether or not it results in an intersection.

#### Example <a name="example4"></a>

```cpp
// Create a 'ViewProperties' struct and set the bounding box dimensions,
// just like in the example up above.
fuchsia::ui::gfx::ViewProperties properties;
properties.bounding_box.min =
    fuchsia::ui::gfx::vec3{.x = 0, .y = 0, .z = 0};
properties.bounding_box.max =
    fuchsia::ui::gfx::vec3{.x = 5, .y = 5, .z = 1};
view_holder.SetViewProperties(std::move(properties));

PointerCommandGenerator pointer(compositor_id, 1,1,
                                 PointerEventType::TOUCH);
// Touch the upper left corner of the display.
session->Enqueue(pointer.Add(0.f, 0.f));
```

This example shows an orthographic camera setup with a view whose min and max bound points are (0,0,0) and (5,5,1) respectively. There is a touch event at the screen space coordinate point (0,0). If there were no corrections to the pixel offset, an orthographic ray generated at the (0,0) point and transformed into world space would wind up grazing against the edge of the bounding box  and would not register as a hit. However, the “Add” command is jittered to (0.5, 0.5) which does actually result in a ray which hits the bounding box. Doing this is the equivalent of running the following command with no jittering:

```cpp
session->Enqueue(pointer.Add(0.5f, 0.5f));
```