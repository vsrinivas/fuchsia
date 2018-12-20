{assignments}

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_{protocol_name_uppercase};
        ddk_proto_ops_ = &ops_;
