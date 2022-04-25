// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/debug_dump.h"

#include "src/devices/lib/bind/ffi_bindings.h"

namespace {

void DumpDevice(VmoWriter* vmo, const Device* dev, size_t indent) {
  zx_koid_t pid = dev->host() ? dev->host()->koid() : 0;
  if (pid == 0) {
    vmo->Printf("%*s[%s]\n", (int)(indent * 3), "", dev->name().data());
  } else {
    vmo->Printf("%*s%c%s%c pid=%zu %s\n", (int)(indent * 3), "",
                dev->flags & DEV_CTX_PROXY ? '<' : '[', dev->name().data(),
                dev->flags & DEV_CTX_PROXY ? '>' : ']', pid, dev->libname().data());
  }
  if (dev->proxy()) {
    indent++;
    DumpDevice(vmo, dev->proxy().get(), indent);
  }
  if (dev->new_proxy()) {
    indent++;
    DumpDevice(vmo, dev->new_proxy().get(), indent);
  }
  for (const auto& child : dev->children()) {
    DumpDevice(vmo, &child, indent + 1);
  }
}

void DumpDriver(const Driver& drv, VmoWriter& writer) {
  writer.Printf("Name    : %s\n", drv.name.c_str());
  writer.Printf("Driver  : %s\n", !drv.libname.empty() ? drv.libname.c_str() : "(null)");
  writer.Printf("Flags   : %#08x\n", drv.flags);
  writer.Printf("Bytecode Version   : %u\n", drv.bytecode_version);

  if (!drv.binding_size) {
    return;
  }

  if (drv.bytecode_version == 1) {
    auto* binding = std::get_if<std::unique_ptr<zx_bind_inst_t[]>>(&drv.binding);
    if (!binding) {
      return;
    }

    char line[256];
    uint32_t count = drv.binding_size / static_cast<uint32_t>(sizeof(binding->get()[0]));
    writer.Printf("Binding : %u instruction%s (%u bytes)\n", count, (count == 1) ? "" : "s",
                  drv.binding_size);
    for (uint32_t i = 0; i < count; ++i) {
      di_dump_bind_inst(&binding->get()[i], line, sizeof(line));
      writer.Printf("[%u/%u]: %s\n", i + 1, count, line);
    }
  } else if (drv.bytecode_version == 2) {
    auto* binding = std::get_if<std::unique_ptr<uint8_t[]>>(&drv.binding);
    if (!binding) {
      return;
    }

    writer.Printf("Bytecode (%u byte%s): ", drv.binding_size, (drv.binding_size == 1) ? "" : "s");
    writer.Printf("%s", dump_bytecode(binding->get(), drv.binding_size));
    writer.Printf("\n\n");
  }
}

void DumpDeviceProps(VmoWriter* vmo, const Device* dev) {
  if (dev->host()) {
    vmo->Printf("Name [%s]%s%s%s\n", dev->name().data(), dev->libname().empty() ? "" : " Driver [",
                dev->libname().empty() ? "" : dev->libname().data(),
                dev->libname().empty() ? "" : "]");
    vmo->Printf("Flags   :%s%s%s%s%s%s\n", dev->flags & DEV_CTX_IMMORTAL ? " Immortal" : "",
                dev->flags & DEV_CTX_MUST_ISOLATE ? " Isolate" : "",
                dev->flags & DEV_CTX_MULTI_BIND ? " MultiBind" : "",
                dev->flags & DEV_CTX_BOUND ? " Bound" : "",
                (dev->state() == Device::State::kDead) ? " Dead" : "",
                dev->flags & DEV_CTX_PROXY ? " Proxy" : "");

    char a = (char)((dev->protocol_id() >> 24) & 0xFF);
    char b = (char)((dev->protocol_id() >> 16) & 0xFF);
    char c = (char)((dev->protocol_id() >> 8) & 0xFF);
    char d = (char)(dev->protocol_id() & 0xFF);
    vmo->Printf("ProtoId : '%c%c%c%c' %#08x(%u)\n", isprint(a) ? a : '.', isprint(b) ? b : '.',
                isprint(c) ? c : '.', isprint(d) ? d : '.', dev->protocol_id(), dev->protocol_id());

    const auto& props = dev->props();
    vmo->Printf("%zu Propert%s\n", props.size(), props.size() == 1 ? "y" : "ies");
    for (uint32_t i = 0; i < props.size(); ++i) {
      const zx_device_prop_t* p = &props[i];
      const char* param_name = di_bind_param_name(p->id);

      if (param_name) {
        vmo->Printf("[%2u/%2zu] : Value %#08x Id %s\n", i, props.size(), p->value, param_name);
      } else {
        vmo->Printf("[%2u/%2zu] : Value %#08x Id %#04hx\n", i, props.size(), p->value, p->id);
      }
    }

    const auto& str_props = dev->str_props();
    vmo->Printf("%zu String Propert%s\n", str_props.size(), str_props.size() == 1 ? "y" : "ies");
    for (uint32_t i = 0; i < str_props.size(); ++i) {
      const StrProperty* p = &str_props[i];
      vmo->Printf("[%2u/%2zu] : %s=", i, str_props.size(), p->key.data());
      std::visit(
          [vmo](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, uint32_t>) {
              vmo->Printf("%#08x\n", arg);
            } else if constexpr (std::is_same_v<T, std::string>) {
              vmo->Printf("\"%s\"\n", arg.data());
            } else if constexpr (std::is_same_v<T, bool>) {
              vmo->Printf("%s\n", arg ? "true" : "false");
            } else {
              vmo->Printf("(unknown value type!)\n");
            }
          },
          p->value);
    }
    vmo->Printf("\n");
  }

  if (dev->proxy()) {
    DumpDeviceProps(vmo, dev->proxy().get());
  }
  if (dev->new_proxy()) {
    DumpDeviceProps(vmo, dev->new_proxy().get());
  }
  for (const auto& child : dev->children()) {
    DumpDeviceProps(vmo, &child);
  }
}

}  // namespace

DebugDump::DebugDump(Coordinator* coordinator) : coordinator_(coordinator) {}

DebugDump::~DebugDump() {}

void DebugDump::DumpTree(DumpTreeRequestView request, DumpTreeCompleter::Sync& completer) {
  VmoWriter writer{std::move(request->output)};
  DumpState(&writer);
  completer.Reply(writer.status(), writer.written(), writer.available());
}

void DebugDump::DumpDrivers(DumpDriversRequestView request, DumpDriversCompleter::Sync& completer) {
  VmoWriter writer{std::move(request->output)};
  for (const auto& drv : coordinator_->drivers()) {
    DumpDriver(drv, writer);
  }

  auto drivers = coordinator_->driver_loader().GetAllDriverIndexDrivers();
  for (const auto& drv : drivers) {
    DumpDriver(*drv, writer);
  }

  completer.Reply(writer.status(), writer.written(), writer.available());
}

void DebugDump::DumpBindingProperties(DumpBindingPropertiesRequestView request,
                                      DumpBindingPropertiesCompleter::Sync& completer) {
  VmoWriter writer{std::move(request->output)};
  DumpDeviceProps(&writer, coordinator_->root_device().get());
  if (coordinator_->sys_device()) {
    DumpDeviceProps(&writer, coordinator_->sys_device().get());
  }
  completer.Reply(writer.status(), writer.written(), writer.available());
}

void DebugDump::DumpState(VmoWriter* vmo) const {
  DumpDevice(vmo, coordinator_->root_device().get(), 0);
  if (coordinator_->sys_device()) {
    DumpDevice(vmo, coordinator_->sys_device().get(), 1);
  }
}
