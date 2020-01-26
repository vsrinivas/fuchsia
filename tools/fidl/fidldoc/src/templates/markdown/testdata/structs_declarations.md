
## **STRUCTS**

### EncodedImage {#EncodedImage}
*Defined in [fuchsia.images/encoded_image.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.images/encoded_image.fidl#7)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>vmo</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td><p>The vmo.</p>
</td>
            <td>No default</td>
        </tr><tr>
            <td><code>size</code></td>
            <td>
                <code>uint64</code>
            </td>
            <td><p>The size of the image in the vmo in bytes.</p>
</td>
            <td>No default</td>
        </tr>
</table>

### ImageInfo {#ImageInfo}
*Defined in [fuchsia.images/image_info.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.images/image_info.fidl#117)*

<p>Information about a graphical image (texture) including its format and size.</p>


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>transform</code></td>
            <td>
                <code><a class='link' href='#Transform'>Transform</a></code>
            </td>
            <td><p>Specifies if the image should be mirrored before displaying.</p>
</td>
            <td><a class='link' href='#Transform.NORMAL'>Transform.NORMAL</a></td>
        </tr><tr>
            <td><code>width</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td><p>The width and height of the image in pixels.</p>
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
            <td><p>The number of bytes per row in the image buffer.</p>
</td>
            <td>No default</td>
        </tr><tr>
            <td><code>pixel_format</code></td>
            <td>
                <code><a class='link' href='#PixelFormat'>PixelFormat</a></code>
            </td>
            <td><p>The pixel format of the image.</p>
</td>
            <td><a class='link' href='#PixelFormat.BGRA_8'>PixelFormat.BGRA_8</a></td>
        </tr><tr>
            <td><code>color_space</code></td>
            <td>
                <code><a class='link' href='#ColorSpace'>ColorSpace</a></code>
            </td>
            <td><p>The pixel color space.</p>
</td>
            <td><a class='link' href='#ColorSpace.SRGB'>ColorSpace.SRGB</a></td>
        </tr><tr>
            <td><code>tiling</code></td>
            <td>
                <code><a class='link' href='#Tiling'>Tiling</a></code>
            </td>
            <td><p>The pixel arrangement in memory.</p>
</td>
            <td><a class='link' href='#Tiling.LINEAR'>Tiling.LINEAR</a></td>
        </tr><tr>
            <td><code>alpha_format</code></td>
            <td>
                <code><a class='link' href='#AlphaFormat'>AlphaFormat</a></code>
            </td>
            <td><p>Specifies the interpretion of the alpha channel, if one exists.</p>
</td>
            <td><a class='link' href='#AlphaFormat.OPAQUE'>AlphaFormat.OPAQUE</a></td>
        </tr>
</table>

### PresentationInfo {#PresentationInfo}
*Defined in [fuchsia.images/presentation_info.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.images/presentation_info.fidl#10)*

<p>Information returned by methods such as <code>ImagePipe.PresentImage()</code> and
<code>Session.Present()</code>, when the consumer begins preparing the first frame
which includes the presented content.</p>


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>presentation_time</code></td>
            <td>
                <code>uint64</code>
            </td>
            <td><p>The actual time at which the enqueued operations are anticipated to take
visible effect, expressed in nanoseconds in the <code>CLOCK_MONOTONIC</code>
timebase.</p>
<p>This value increases monotonically with each new frame, typically in
increments of the <code>presentation_interval</code>.</p>
</td>
            <td>No default</td>
        </tr><tr>
            <td><code>presentation_interval</code></td>
            <td>
                <code>uint64</code>
            </td>
            <td><p>The nominal amount of time which is anticipated to elapse between
successively presented frames, expressed in nanoseconds.  When rendering
to a display, the interval will typically be derived from the display
refresh rate.</p>
<p>This value is non-zero.  It may vary from time to time, such as when
changing display modes.</p>
</td>
            <td>No default</td>
        </tr>
</table>
