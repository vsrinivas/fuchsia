{% block body %}
<devsite-mathjax config="TeX-AMS-MML_SVG"></devsite-mathjax>

# Fuchsia Accessibility Magnifier

## Overview

Low-vision individuals can often benefit from magnifying portions of the
screen. The fuchsia magnifier is an assistive technology that enables these
users to zoom and pan across the screen via touch input gestures.

## UX

UX
Magnification is enabled/disabled through device settings. “Enabling” magnification
does not itself magnify the screen; rather, it enables the user to magnify the
screen explicitly via specified gestures.

Once the magnifier is enabled, we can summarize its UX via the state diagram below.
The three pertinent magnifier states are:

1. **Unmagnified**: In the **unmagnified** state, the user has not yet magnified the
screen, or has explicitly returned to normal zoom.
1. **Temporarily magnified**: If the screen is **temporarily magnified**, the user
can move their finger(s) across the screen to focus whatever would be under the
finger in the **unmagnified** state. The user can enter this state by holding the
last tap of either a one-finger-triple-tap or a three-finger-double-tap. The screen
is magnified at a default scale of 4 (content appears 4 times larger than normal) for
the duration of the held tap, and returns to **unmagnified** once the user lifts
their finger(s).
1. **Persistently magnified**: The user can **persistently magnify** the screen via
a one-finger-triple-tap or a three-finger-double-tap (without holding the last tap).
In this case, the screen will remain magnified until the user explicitly returns to
the **unmagnified** state with another one-finger-triple-tap or
three-finger-double-tap. During the interceding period while the screen is magnified,
the magnifier is responsive to two-finger-drag gestures, via which the user can pan
and zoom. In this case, the new magnification focus is computed based on the direction
and magnitude of the drag, and accounts for the magnified scale (so a drag covering
the same absolute physical distance will result in a smaller change when the
magnification scale is larger). Note that in this case, the panning is also similar
to a normal touch screen experience in that the pan direction is opposite the gesture
direction (e.g. dragging to the left moves the focus of the magnification to the right).

![This figure shows a state diagram for the magnifier UX. Transitions:
unmagnified -> temporarily magnified: Hold last tap of one-finger-triple-tap or
three-finger-double-tap
temporarily magnified -> temporarily magnified: Drag held tap
temporarily magnified -> unmagnified: Release hold
unmagnified -> persistently magnified: One-finger-triple-tap or three-finger-double-tap
(no hold on last tap)
persistently magnified -> persistently magnified: Two-finger drag
persistently magnified -> persistently magnified: One-finger-triple-tap or
three-finger-double-tap (no hold on last tap)](images/magnifier_ux_state_diagram.png)

## Gesture recognition

In order to respond to touch input, the magnifier must be represented in the
accessibility manager's [gesture recognition arena][accessibility-input]. Furthermore,
since magnifier gestures should take precedence over screen reader gestures, the gesture
arena needs to give precedence to magnification-specific gesture recognizers. In practice,
we ensure this ordering by registering the magnifier’s recognizers with the gesture arena
before the screen reader’s.

## Clip Space Transform

The current magnifier is built around scenic’s clip-space transform. The clip-space
transform scales the [NDC coordinate space][camera-transform] about the origin, and then
translates the scaled space relative to the "camera", an abstraction of the graphics
compositor that represents a perspective on rendered content.

The clip-space transform comprises two important elements:

1. **Scale factor** (scalar): The scale factor is a float >= 1, where scale == 1 represents
the default, zoomless state. This scale factor is applied to both the x and y axes.
1. **Translation** (vector): The translation is a two-dimensional vector applied
additively **after** the scale factor has been applied.

So, the location for some NDC location (x, y) when magnified by scale S with
translation $$(T_x, T_y)$$ is $$(Sx + T_x, Sy + T_y)$$.

After the transform is applied to the NDC coordinate space, only the portion of the scaled
space for which $$-1 <= x <= 1$$ and $$-1 <= y <= 1$$ is displayed. We refer to this portion of
the scaled space as the “magnification viewport".

Consider this example of magnifying the screen when the image below is displayed (viewport
shown in dotted red lines):

![In the left side of the figure, we see a grid of six pictures representing possible screen
content and the NDC coordinate space overlaid onto the image. The bottom-left corner of both
the NDC space and the screen content  are shown to be (-1, -1). The top-right corner of both
the NDC space and screen content are shown to be (1, 1). We also see a red dot over a
recognizable point in one of the six pictures in the hypothetical screen contents. This half
of the figure represents the unmagnified state. In the right half of the picture, we see the
same grid of six pictures magnified to a scale > 1, and the NDC coordinate space again
overlaid onto the image. The bottom-left corner of the magnified view contents is now shown
to be at a point for which x < -1 and y < -1, and the top-right corner of the magnified view
contents is shown to be at a point for which x > 1, y > 1. We also see that the origin of
the NDC space corresponds to a different point in the hypothetical screen contents to show
that some translation has occurred in addition to the scaling. Finally, we see that the red
dot from the left half of the figure still corresponds to the same point in the magnified
screen contents.](images/magnification_example.png)

Notice that viewport remains fixed between $$(-1, -1)$$ and $$(1, 1)$$, but the view content
itself scales and translates down and to the left.

Note also that incoming pointer event NDC coordinates will always be relative to the viewport,
which is fixed between $$(-1, -1)$$ and $$(1, 1)$$ on both axes. Consequently, our gesture
recognition logic, which relies on NDC, scales implicitly.


## Magnifier math

Since the UX is different for temporary vs. persistent magnification, the math is also
slightly different between the two.

### Temporary magnification mode

For temporary magnification, we need to magnify the screen to the default scale using the point
directly under the user’s finger in **unscaled** space as the focus of magnification. So, the
goal of the transform we specify is to first scale the coordinate space about NDC coordinate
(0, 0), and then translate the resulting space such that the focal point of the magnification
(the point under the user’s finger in the unzoomed space) is also under the user’s finger in
the magnified view. The purpose of this choice, as opposed to simply centering the focal point
onscreen (i.e. translating such that the focal point is at (0,0) is to avoid the magnified view
going offscreen, e.g. if the user’s finger is close to the boundary of the screen. In order to
achieve this goal, the translation we specify should be (note that focus is a
vector):

<div>
  $$
  focus - (scale * focus) = (1 - scale) * focus = -focus * (scale - 1)
  $$
</div>

To visualize, we are interested in the red arrow below:

![Depicts a coordinate plane with origin (0, 0) at the center.
There are two points marked in Quadrant 1, where one is twice as far from the
origin as the other, and both points and the origin are on the same line. The
closer point to the origin represents the user’s finger location in NDC space,
and the more distant point represents the scaled coordinates of the user’s
finger. The distance from the origin to the first point is marked as
(focus_vector), the distance from origin to the second point is marked as
(focus_vector * scale), and the distance between them is marked as (focus_vector
- (focus_vector * scale)).](images/magnifier_math_example.png)

### Persistent Magnification Mode

In persistent magnification, the user can pan and zoom using a two-finger drag
gesture.

As the distance between the user’s fingers changes, the zoom should change
proportionally to the change in distance (e.g. moving the fingers twice as
far apart will cause the scale to increase by a factor of 2).

To achieve panning, our goal is to keep the same point in unscaled space under
the centroid of the drag at all times. To do so, we need to consider both the
new and previous locations of the centroid (midpoint) between the two fingers.
First, we can determine the point in unscaled space that is under the previous
centroid of the two fingers by applying the inverse of the current
magnification transform to the previous centroid coordinates. Then, we can
compute the new magnification transform by determining what translation will
place the unscaled point at the new centroid location (after we’ve applied the
new scale factor). We solve for the new magnification transform below.

**Variable definitions**

$$scale_i$$ = Initial scale (scale for the previous magnification transform) (scalar)

$$scale_f$$ = Final scale (scale for the current magnification transform) (scalar)

$$translation_i$$ = Initial translation (vector)

$$centroid_i$$ = Initial centroid (vector)

$$centroid_f$$ = Final centroid (vector)

$$\Delta centroid = centroid_f - centroid_i$$ (vector)

$$spread_i$$ = Initial separation between the two fingers (scalar)

$$spread_f$$ = Final separation between the two fingers (scalar)

$$centroid_u$$ = Initial centroid in unscaled NDC space (vector)

**Unknown**

$$translation_f$$ = Final translation (vector)

**Derivation**

*First, compute the new scale.*
$$
scale_f = scale_i * (spread_f / spread_i)
$$

*Then, compute the unscaled point that corresponds to the location of the previous
centroid.*
$$
centroid_u = (centroid_i - translation_i) / scale_i
$$

*Next, set $$centroid_f$$ (in new-scale space) equal to $$T(centroid_u)$$, where T
is the new transform.*
$$
(centroid_u * scale_f) + translation_f = centroid_f
$$

*Rearrange to solve for new_translation (the unknown of interest).*
$$
translation_f = centroid_f - (centroid_u * scale_f)
$$

*Substitute for $$centroid_u$$.*
$$
translation_f = centroid_f - ((centroid_i - translation_i) / scale_i * scale_f)
$$

*Simplify.*
$$
              = centroid_f + (translation_i - centroid_i) * (scale_f / scale_i)
$$

*Substitute $$centroid_i + \Delta centroid$$ so that we don’t have to keep track of
both old_centroid and new_centroid simultaneously (i.e. we can do this calculation
before we update the gesture state to reflect the new finger locations).*
$$
= centroid_i + \Delta centroid + (translation_i - centroid_i) * (scale_f / scale_i)
$$

[accessibility-input]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=78638
[camera-transform]: /docs/development/graphics/scenic/concepts/view_bounds.md

{% endblock %}
