{assignments}

        if constexpr (internal::is_base_proto<Base>::value) {{
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_{protocol_name_uppercase};
            dev->ddk_proto_ops_ = &{protocol_name}_protocol_ops_;
        }}
