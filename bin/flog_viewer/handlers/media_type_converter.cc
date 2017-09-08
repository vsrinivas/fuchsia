// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garent/bin/flog_viewer/handlers/media_type_converter.h"

#include <iostream>

#include "lib/media/fidl/logs/media_type_converter_channel.fidl.h"
#include "garent/bin/flog_viewer/flog_viewer.h"
#include "garent/bin/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaTypeConverter::MediaTypeConverter(const std::string& format)
    : ChannelHandler(format),
      accumulator_(std::make_shared<MediaTypeConverterAccumulator>()) {
  stub_.set_sink(this);
}

MediaTypeConverter::~MediaTypeConverter() {}

void MediaTypeConverter::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaTypeConverter::GetAccumulator() {
  return accumulator_;
}

void MediaTypeConverter::BoundAs(uint64_t koid,
                                 const fidl::String& converter_type) {
  terse_out() << EntryHeader(entry(), entry_index())
              << "MediaTypeConverter.BoundAs\n";
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << "\n";
  terse_out() << begl << "converter_type: " << converter_type << "\n";
  terse_out() << outdent;

  BindAs(koid);
  accumulator_->converter_type_ = converter_type;
}

void MediaTypeConverter::Config(media::MediaTypePtr input_type,
                                media::MediaTypePtr output_type,
                                uint64_t consumer_address,
                                uint64_t producer_address) {
  FTL_DCHECK(input_type);
  FTL_DCHECK(output_type);

  terse_out() << EntryHeader(entry(), entry_index())
              << "MediaTypeConverter.Config\n";
  terse_out() << indent;
  terse_out() << begl << "input_type: " << input_type << "\n";
  terse_out() << begl << "output_type: " << output_type << "\n";
  terse_out() << begl << "consumer_address: " << *AsChannel(consumer_address)
              << "\n";
  terse_out() << begl << "producer_address: " << *AsChannel(producer_address)
              << "\n";
  terse_out() << outdent;

  accumulator_->input_type_ = std::move(input_type);
  accumulator_->output_type_ = std::move(output_type);
  accumulator_->consumer_channel_ = AsChannel(consumer_address);
  accumulator_->consumer_channel_->SetHasParent();
  accumulator_->producer_channel_ = AsChannel(producer_address);
  accumulator_->producer_channel_->SetHasParent();
}

MediaTypeConverterAccumulator::MediaTypeConverterAccumulator() {}

MediaTypeConverterAccumulator::~MediaTypeConverterAccumulator() {}

void MediaTypeConverterAccumulator::Print(std::ostream& os) {
  os << "MediaTypeConverter\n";
  os << indent;
  os << begl << "converter_type: " << converter_type_ << "\n";
  os << begl << "input_type: " << input_type_ << "\n";
  os << begl << "output_type: " << output_type_ << "\n";

  if (consumer_channel_) {
    os << begl << "consumer: " << *consumer_channel_ << " ";
    FTL_DCHECK(consumer_channel_->resolved());
    consumer_channel_->PrintAccumulator(os);
    os << "\n";
  } else {
    os << begl << "consumer: <none>\n";
  }

  if (producer_channel_) {
    os << begl << "producer: " << *producer_channel_ << " ";
    FTL_DCHECK(producer_channel_->resolved());
    producer_channel_->PrintAccumulator(os);
  } else {
    os << begl << "producer: <none>";
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
