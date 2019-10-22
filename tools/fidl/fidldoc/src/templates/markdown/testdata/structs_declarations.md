
## **STRUCTS**

### EncodedImage {:#EncodedImage}
*Defined in [fuchsia.images/encoded_image.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.images/encoded_image.fidl#7)*





<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>vmo</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td> The vmo.
</td>
            <td>No default</td>
        </tr><tr>
            <td><code>size</code></td>
            <td>
                <code>uint64</code>
            </td>
            <td> The size of the image in the vmo in bytes.
</td>
            <td>No default</td>
        </tr>
</table>

### ImageInfo {:#ImageInfo}
*Defined in [fuchsia.images/image_info.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.images/image_info.fidl#117)*



 Information about a graphical image (texture) including its format and size.


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>transform</code></td>
            <td>
                <code><a class='link' href='#Transform'>Transform</a></code>
            </td>
            <td> Specifies if the image should be mirrored before displaying.
</td>
            <td><a class='link' href='#Transform.NORMAL'>Transform.NORMAL</a></td>
        </tr><tr>
            <td><code>width</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td> The width and height of the image in pixels.
</td>
            <td>No default</td>
        </tr><tr>
            <td><code>height</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>stride</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td> The number of bytes per row in the image buffer.
</td>
            <td>No default</td>
        </tr><tr>
            <td><code>pixel_format</code></td>
            <td>
                <code><a class='link' href='#PixelFormat'>PixelFormat</a></code>
            </td>
            <td> The pixel format of the image.
</td>
            <td><a class='link' href='#PixelFormat.BGRA_8'>PixelFormat.BGRA_8</a></td>
        </tr><tr>
            <td><code>color_space</code></td>
            <td>
                <code><a class='link' href='#ColorSpace'>ColorSpace</a></code>
            </td>
            <td> The pixel color space.
</td>
            <td><a class='link' href='#ColorSpace.SRGB'>ColorSpace.SRGB</a></td>
        </tr><tr>
            <td><code>tiling</code></td>
            <td>
                <code><a class='link' href='#Tiling'>Tiling</a></code>
            </td>
            <td> The pixel arrangement in memory.
</td>
            <td><a class='link' href='#Tiling.LINEAR'>Tiling.LINEAR</a></td>
        </tr><tr>
            <td><code>alpha_format</code></td>
            <td>
                <code><a class='link' href='#AlphaFormat'>AlphaFormat</a></code>
            </td>
            <td> Specifies the interpretion of the alpha channel, if one exists.
</td>
            <td><a class='link' href='#AlphaFormat.OPAQUE'>AlphaFormat.OPAQUE</a></td>
        </tr>
</table>

### PresentationInfo {:#PresentationInfo}
*Defined in [fuchsia.images/presentation_info.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.images/presentation_info.fidl#10)*



 Information returned by methods such as `ImagePipe.PresentImage()` and
 `Session.Present()`, when the consumer begins preparing the first frame
 which includes the presented content.


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>presentation_time</code></td>
            <td>
                <code>uint64</code>
            </td>
            <td> The actual time at which the enqueued operations are anticipated to take
 visible effect, expressed in nanoseconds in the `CLOCK_MONOTONIC`
 timebase.

 This value increases monotonically with each new frame, typically in
 increments of the `presentation_interval`.
</td>
            <td>No default</td>
        </tr><tr>
            <td><code>presentation_interval</code></td>
            <td>
                <code>uint64</code>
            </td>
            <td> The nominal amount of time which is anticipated to elapse between
 successively presented frames, expressed in nanoseconds.  When rendering
 to a display, the interval will typically be derived from the display
 refresh rate.

 This value is non-zero.  It may vary from time to time, such as when
 changing display modes.
</td>
            <td>No default</td>
        </tr>
</table>
