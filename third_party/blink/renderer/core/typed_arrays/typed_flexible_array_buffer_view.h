// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_TYPED_ARRAYS_TYPED_FLEXIBLE_ARRAY_BUFFER_VIEW_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_TYPED_ARRAYS_TYPED_FLEXIBLE_ARRAY_BUFFER_VIEW_H_

#include "base/macros.h"
#include "third_party/blink/renderer/core/typed_arrays/array_buffer/typed_array.h"
#include "third_party/blink/renderer/core/typed_arrays/flexible_array_buffer_view.h"

namespace blink {

template <typename TypedArray>
class TypedFlexibleArrayBufferView final : public FlexibleArrayBufferView {
  STACK_ALLOCATED();

 public:
  using ValueType = typename TypedArray::ValueType;

  TypedFlexibleArrayBufferView() = default;
  TypedFlexibleArrayBufferView(const TypedFlexibleArrayBufferView&) = default;
  TypedFlexibleArrayBufferView(TypedFlexibleArrayBufferView&&) = default;
  TypedFlexibleArrayBufferView(v8::Local<v8::ArrayBufferView> array_buffer_view)
      : FlexibleArrayBufferView(array_buffer_view) {}
  ~TypedFlexibleArrayBufferView() = default;

  ValueType* DataMaybeOnStack() const {
    return static_cast<ValueType*>(BaseAddressMaybeOnStack());
  }

  size_t lengthAsSizeT() const {
    DCHECK_EQ(ByteLengthAsSizeT() % sizeof(ValueType), 0u);
    return ByteLengthAsSizeT() / sizeof(ValueType);
  }
};

using FlexibleInt8Array = TypedFlexibleArrayBufferView<TypedArray<int8_t>>;
using FlexibleInt16Array = TypedFlexibleArrayBufferView<TypedArray<int16_t>>;
using FlexibleInt32Array = TypedFlexibleArrayBufferView<TypedArray<int32_t>>;
using FlexibleUint8Array = TypedFlexibleArrayBufferView<TypedArray<uint8_t>>;
using FlexibleUint8ClampedArray =
    TypedFlexibleArrayBufferView<TypedArray<uint8_t, true>>;
using FlexibleUint16Array = TypedFlexibleArrayBufferView<TypedArray<uint16_t>>;
using FlexibleUint32Array = TypedFlexibleArrayBufferView<TypedArray<uint32_t>>;
using FlexibleBigInt64Array = TypedFlexibleArrayBufferView<TypedArray<int64_t>>;
using FlexibleBigUint64Array =
    TypedFlexibleArrayBufferView<TypedArray<uint64_t>>;
using FlexibleFloat32Array = TypedFlexibleArrayBufferView<TypedArray<float>>;
using FlexibleFloat64Array = TypedFlexibleArrayBufferView<TypedArray<double>>;

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_TYPED_ARRAYS_TYPED_FLEXIBLE_ARRAY_BUFFER_VIEW_H_
