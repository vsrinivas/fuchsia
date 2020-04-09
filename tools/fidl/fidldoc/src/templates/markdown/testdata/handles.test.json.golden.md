[TOC]

# fidl.test.handles


## **PROTOCOLS**

## SomeProtocol {#SomeProtocol}
*Defined in [fidl.test.handles/handles.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#3)*




## **STRUCTS**

### Handles {#Handles}
*Defined in [fidl.test.handles/handles.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#10)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>plain_handle</code></td>
            <td>
                <code>handle&lt;handle&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>bti_handle</code></td>
            <td>
                <code>handle&lt;bti&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>channel_handle</code></td>
            <td>
                <code>handle&lt;channel&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>clock_handle</code></td>
            <td>
                <code>handle&lt;clock&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>debuglog_handle</code></td>
            <td>
                <code>handle&lt;debuglog&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>event_handle</code></td>
            <td>
                <code>handle&lt;event&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>eventpair_handle</code></td>
            <td>
                <code>handle&lt;eventpair&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>exception_handle</code></td>
            <td>
                <code>handle&lt;exception&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>fifo_handle</code></td>
            <td>
                <code>handle&lt;fifo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>guest_handle</code></td>
            <td>
                <code>handle&lt;guest&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>interrupt_handle</code></td>
            <td>
                <code>handle&lt;interrupt&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>iommu_handle</code></td>
            <td>
                <code>handle&lt;iommu&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>job_handle</code></td>
            <td>
                <code>handle&lt;job&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>pager_handle</code></td>
            <td>
                <code>handle&lt;pager&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>pcidevice_handle</code></td>
            <td>
                <code>handle&lt;pcidevice&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>pmt_handle</code></td>
            <td>
                <code>handle&lt;pmt&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>port_handle</code></td>
            <td>
                <code>handle&lt;port&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>process_handle</code></td>
            <td>
                <code>handle&lt;process&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>profile_handle</code></td>
            <td>
                <code>handle&lt;profile&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>resource_handle</code></td>
            <td>
                <code>handle&lt;resource&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>socket_handle</code></td>
            <td>
                <code>handle&lt;socket&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>suspendtoken_handle</code></td>
            <td>
                <code>handle&lt;suspendtoken&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>thread_handle</code></td>
            <td>
                <code>handle&lt;thread&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>timer_handle</code></td>
            <td>
                <code>handle&lt;timer&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>vcpu_handle</code></td>
            <td>
                <code>handle&lt;vcpu&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>vmar_handle</code></td>
            <td>
                <code>handle&lt;vmar&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>vmo_handle</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>rights_handle</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>aliased_plain_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_plain_handle'>aliased_plain_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>aliased_subtype_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_subtype_handle'>aliased_subtype_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>aliased_rights_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_rights_handle'>aliased_rights_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>some_protocol</code></td>
            <td>
                <code><a class='link' href='#SomeProtocol'>SomeProtocol</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>request_some_protocol</code></td>
            <td>
                <code>request&lt;<a class='link' href='#SomeProtocol'>SomeProtocol</a>&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>













## **TYPE ALIASES**

<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="aliased_plain_handle">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#6">aliased_plain_handle</a></td>
            <td>
                <code>handle</code></td>
            <td></td>
        </tr><tr id="aliased_subtype_handle">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#7">aliased_subtype_handle</a></td>
            <td>
                <code>handle</code></td>
            <td></td>
        </tr><tr id="aliased_rights_handle">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#8">aliased_rights_handle</a></td>
            <td>
                <code>handle</code></td>
            <td></td>
        </tr></table>

