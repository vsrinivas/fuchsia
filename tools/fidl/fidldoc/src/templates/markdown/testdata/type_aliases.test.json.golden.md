[TOC]

# example




## **STRUCTS**

### ExampleOfUseOfAliases {#ExampleOfUseOfAliases}
*Defined in [example/example.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#27)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="ExampleOfUseOfAliases.field_of_u32">
            <td><code>field_of_u32</code></td>
            <td>
                <code><a class='link' href='#u32'>u32</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_vec_at_most_five_of_string">
            <td><code>field_of_vec_at_most_five_of_string</code></td>
            <td>
                <code><a class='link' href='#vec_at_most_five'>vec_at_most_five</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_vec_at_most_five_of_uint32">
            <td><code>field_of_vec_at_most_five_of_uint32</code></td>
            <td>
                <code><a class='link' href='#vec_at_most_five'>vec_at_most_five</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_vec_of_strings">
            <td><code>field_of_vec_of_strings</code></td>
            <td>
                <code><a class='link' href='#vec_of_strings'>vec_of_strings</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_vec_of_strings_at_most_nine">
            <td><code>field_of_vec_of_strings_at_most_nine</code></td>
            <td>
                <code><a class='link' href='#vec_of_strings'>vec_of_strings</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_vec_of_strings_at_most_5">
            <td><code>field_of_vec_of_strings_at_most_5</code></td>
            <td>
                <code><a class='link' href='#vec_of_strings_at_most_5'>vec_of_strings_at_most_5</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_vec_at_most_5_of_reference_me">
            <td><code>field_of_vec_at_most_5_of_reference_me</code></td>
            <td>
                <code><a class='link' href='#vec_at_most_5'>vec_at_most_5</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_channel">
            <td><code>field_of_channel</code></td>
            <td>
                <code><a class='link' href='#channel'>channel</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_client_end">
            <td><code>field_of_client_end</code></td>
            <td>
                <code><a class='link' href='#client_end'>client_end</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="ExampleOfUseOfAliases.field_of_nullable_client_end">
            <td><code>field_of_nullable_client_end</code></td>
            <td>
                <code><a class='link' href='#client_end'>client_end</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>



## **ENUMS**

### obj_type {#obj_type}
Type: <code>uint32</code>

*Defined in [example/example.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#8)*



<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="obj_type.NONE">
            <td><code>NONE</code></td>
            <td><code>0</code></td>
            <td></td>
        </tr><tr id="obj_type.CHANNEL">
            <td><code>CHANNEL</code></td>
            <td><code>4</code></td>
            <td></td>
        </tr></table>











## **TYPE ALIASES**

<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="u32">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#19">u32</a></td>
            <td>
                <code>uint32</code></td>
            <td></td>
        </tr><tr id="vec_at_most_five">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#20">vec_at_most_five</a></td>
            <td>
                <code>example/vector</code>[<code>5</code>]</td>
            <td></td>
        </tr><tr id="vec_of_strings">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#21">vec_of_strings</a></td>
            <td>
                <code>vector</code></td>
            <td></td>
        </tr><tr id="vec_of_strings_at_most_5">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#22">vec_of_strings_at_most_5</a></td>
            <td>
                <code>vector</code>[<code>5</code>]</td>
            <td></td>
        </tr><tr id="vec_at_most_5">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#23">vec_at_most_5</a></td>
            <td>
                <code>example/vector</code>[<code>5</code>]</td>
            <td></td>
        </tr><tr id="channel">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#24">channel</a></td>
            <td>
                <code>handle</code></td>
            <td></td>
        </tr><tr id="client_end">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/example.test.fidl#25">client_end</a></td>
            <td>
                <code>handle</code></td>
            <td></td>
        </tr></table>

