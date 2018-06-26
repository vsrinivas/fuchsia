// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/observation.h>

namespace cobalt_client {

Counter::Counter(Counter&& other)
    : name_(other.name_) {
    counter_.store(other.Exchange(0));
    metric_id_ = other.metric_id_;
    encoding_id_ = other.encoding_id_;
}

ObservationValue Counter::GetObservationValue() const {
    ObservationValue value;
    value.name.size = name_.size();
    value.name.data = const_cast<char*>(name_.c_str());
    value.encoding_id = encoding_id_;
    value.value = IntValue(this->Load());
    return value;
}

ObservationValue Counter::GetObservationValueAndExchange(Counter::Type val) {
    ObservationValue value;
    value.name.size = name_.size();
    value.name.data = const_cast<char*>(name_.c_str());
    value.encoding_id = encoding_id_;
    value.value = IntValue(this->Exchange(val));
    return value;
}

} // namespace cobalt_client
