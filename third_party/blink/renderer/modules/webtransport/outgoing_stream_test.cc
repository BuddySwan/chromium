// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/webtransport/outgoing_stream.h"

#include <utility>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_tester.h"
#include "third_party/blink/renderer/bindings/core/v8/script_value.h"
#include "third_party/blink/renderer/bindings/core/v8/to_v8_for_core.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_testing.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_dom_exception.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/streams/writable_stream.h"
#include "third_party/blink/renderer/core/streams/writable_stream_default_writer.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/modules/webtransport/mock_web_transport_close_proxy.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"

namespace blink {

namespace {

using ::testing::ElementsAre;
using ::testing::StrictMock;

class OutgoingStreamTest : public ::testing::Test {
 public:
  // The default value of |capacity| means some sensible value selected by mojo.
  void CreateDataPipe(uint32_t capacity = 0) {
    MojoCreateDataPipeOptions options;
    options.struct_size = sizeof(MojoCreateDataPipeOptions);
    options.flags = MOJO_CREATE_DATA_PIPE_FLAG_NONE;
    options.element_num_bytes = 1;
    options.capacity_num_bytes = capacity;

    MojoResult result = mojo::CreateDataPipe(&options, &data_pipe_producer_,
                                             &data_pipe_consumer_);
    if (result != MOJO_RESULT_OK) {
      ADD_FAILURE() << "CreateDataPipe() returned " << result;
    }
  }

  OutgoingStream* CreateOutgoingStream(const V8TestingScope& scope,
                                       uint32_t capacity = 0) {
    CreateDataPipe(capacity);
    auto* script_state = scope.GetScriptState();
    DCHECK(!mock_close_proxy_);
    mock_close_proxy_ =
        MakeGarbageCollected<StrictMock<MockWebTransportCloseProxy>>();
    auto* outgoing_stream = MakeGarbageCollected<OutgoingStream>(
        script_state, mock_close_proxy_, std::move(data_pipe_producer_));
    outgoing_stream->Init();
    return outgoing_stream;
  }

  // Reads everything from data_pipe_consumer_ and returns it in a
  // vector.
  Vector<uint8_t> ReadAllPendingData() {
    Vector<uint8_t> data;
    const void* buffer = nullptr;
    uint32_t buffer_num_bytes = 0;
    MojoResult result = data_pipe_consumer_->BeginReadData(
        &buffer, &buffer_num_bytes, MOJO_BEGIN_READ_DATA_FLAG_NONE);

    switch (result) {
      case MOJO_RESULT_OK:
        break;

      case MOJO_RESULT_SHOULD_WAIT:  // No more data yet.
        return data;

      default:
        ADD_FAILURE() << "BeginReadData() failed: " << result;
        return data;
    }

    data.Append(static_cast<const uint8_t*>(buffer), buffer_num_bytes);
    data_pipe_consumer_->EndReadData(buffer_num_bytes);
    return data;
  }

  Persistent<MockWebTransportCloseProxy> mock_close_proxy_;
  mojo::ScopedDataPipeProducerHandle data_pipe_producer_;
  mojo::ScopedDataPipeConsumerHandle data_pipe_consumer_;
};

TEST_F(OutgoingStreamTest, Create) {
  V8TestingScope scope;
  auto* outgoing_stream = CreateOutgoingStream(scope);
  EXPECT_TRUE(outgoing_stream->writable());
}

TEST_F(OutgoingStreamTest, AbortWriting) {
  V8TestingScope scope;
  auto* outgoing_stream = CreateOutgoingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* writer =
      outgoing_stream->writable()->getWriter(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise closed_promise = writer->closed(script_state);

  ScriptPromise writing_aborted = outgoing_stream->writingAborted();

  outgoing_stream->abortWriting();

  ScriptPromiseTester abort_tester(script_state, writing_aborted);
  abort_tester.WaitUntilSettled();
  EXPECT_TRUE(abort_tester.IsFulfilled());

  ScriptPromiseTester closed_tester(script_state, closed_promise);
  closed_tester.WaitUntilSettled();
  EXPECT_TRUE(closed_tester.IsRejected());
  DOMException* closed_exception = V8DOMException::ToImplWithTypeCheck(
      scope.GetIsolate(), closed_tester.Value().V8Value());
  ASSERT_TRUE(closed_exception);
  EXPECT_EQ(closed_exception->name(), "AbortError");
  EXPECT_EQ(closed_exception->message(), "The stream was aborted locally");
}

TEST_F(OutgoingStreamTest, WriteArrayBuffer) {
  V8TestingScope scope;
  auto* outgoing_stream = CreateOutgoingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* writer =
      outgoing_stream->writable()->getWriter(script_state, ASSERT_NO_EXCEPTION);
  auto* chunk = DOMArrayBuffer::Create("A", 1);
  ScriptPromise result =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(scope.GetScriptState(), result);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
  EXPECT_THAT(ReadAllPendingData(), ElementsAre('A'));
}

TEST_F(OutgoingStreamTest, WriteArrayBufferView) {
  V8TestingScope scope;
  auto* outgoing_stream = CreateOutgoingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* writer =
      outgoing_stream->writable()->getWriter(script_state, ASSERT_NO_EXCEPTION);
  auto buffer = ArrayBuffer::Create("*B", 2);
  // Create a view into the buffer with offset 1, ie. "B".
  auto* chunk = DOMUint8Array::Create(buffer, 1, 1);
  ScriptPromise result =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(scope.GetScriptState(), result);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());
  EXPECT_THAT(ReadAllPendingData(), ElementsAre('B'));
}

bool IsAllNulls(base::span<const uint8_t> data) {
  return std::all_of(data.begin(), data.end(), [](uint8_t c) { return !c; });
}

TEST_F(OutgoingStreamTest, AsyncWrite) {
  V8TestingScope scope;
  // Set a large pipe capacity, so any platform-specific excess is dwarfed in
  // size.
  constexpr uint32_t kPipeCapacity = 512u * 1024u;
  auto* outgoing_stream = CreateOutgoingStream(scope, kPipeCapacity);

  auto* script_state = scope.GetScriptState();
  auto* writer =
      outgoing_stream->writable()->getWriter(script_state, ASSERT_NO_EXCEPTION);

  // Write a chunk that definitely will not fit in the pipe.
  const size_t kChunkSize = kPipeCapacity * 3;
  auto* chunk = DOMArrayBuffer::Create(kChunkSize, 1);
  ScriptPromise result =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(scope.GetScriptState(), result);

  // Let the first pipe write complete.
  test::RunPendingTasks();

  // Let microtasks run just in case write() returns prematurely.
  v8::MicrotasksScope::PerformCheckpoint(scope.GetIsolate());
  EXPECT_FALSE(tester.IsFulfilled());

  // Read the first part of the data.
  auto data1 = ReadAllPendingData();
  EXPECT_LT(data1.size(), kChunkSize);

  // Verify the data wasn't corrupted.
  EXPECT_TRUE(IsAllNulls(data1));

  // Allow the asynchronous pipe write to happen.
  test::RunPendingTasks();

  // Read the second part of the data.
  auto data2 = ReadAllPendingData();
  EXPECT_TRUE(IsAllNulls(data2));

  test::RunPendingTasks();

  // Read the final part of the data.
  auto data3 = ReadAllPendingData();
  EXPECT_TRUE(IsAllNulls(data3));
  EXPECT_EQ(data1.size() + data2.size() + data3.size(), kChunkSize);

  // Now the write() should settle.
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  // Nothing should be left to read.
  EXPECT_THAT(ReadAllPendingData(), ElementsAre());
}

// Writing immediately followed by closing should not lose data.
TEST_F(OutgoingStreamTest, WriteThenClose) {
  V8TestingScope scope;

  auto* outgoing_stream = CreateOutgoingStream(scope);
  auto* script_state = scope.GetScriptState();
  auto* writer =
      outgoing_stream->writable()->getWriter(script_state, ASSERT_NO_EXCEPTION);
  auto* chunk = DOMArrayBuffer::Create("D", 1);
  ScriptPromise write_promise =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);

  EXPECT_CALL(*mock_close_proxy_, SendFin());

  ScriptPromise close_promise =
      writer->close(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester write_tester(scope.GetScriptState(), write_promise);
  ScriptPromiseTester close_tester(scope.GetScriptState(), close_promise);

  // Make sure that write() and close() both run before the event loop is
  // serviced.
  v8::MicrotasksScope::PerformCheckpoint(scope.GetIsolate());

  write_tester.WaitUntilSettled();
  EXPECT_TRUE(write_tester.IsFulfilled());
  close_tester.WaitUntilSettled();
  EXPECT_TRUE(close_tester.IsFulfilled());

  EXPECT_THAT(ReadAllPendingData(), ElementsAre('D'));
}

// A live stream will be kept alive even if there is no explicit reference.
// When the underlying connection is shut down, the connection will be swept.
TEST_F(OutgoingStreamTest, GarbageCollection) {
  V8TestingScope scope;

  WeakPersistent<OutgoingStream> outgoing_stream;

  {
    // The writable stream created when creating a OutgoingStream creates some
    // v8 handles. To ensure these are collected, we need to create a handle
    // scope. This is not a problem for garbage collection in normal operation.
    v8::HandleScope handle_scope(scope.GetIsolate());

    outgoing_stream = CreateOutgoingStream(scope);
  }

  // Pretend the stack is empty. This will avoid accidentally treating any
  // copies of the |outgoing_stream| pointer as references.
  V8GCController::CollectAllGarbageForTesting(
      scope.GetIsolate(), v8::EmbedderHeapTracer::EmbedderStackState::kEmpty);

  ASSERT_TRUE(outgoing_stream);

  auto* script_state = scope.GetScriptState();

  EXPECT_CALL(*mock_close_proxy_, SendFin());

  ScriptPromise close_promise =
      outgoing_stream->writable()->close(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromiseTester tester(script_state, close_promise);
  tester.WaitUntilSettled();
  EXPECT_TRUE(tester.IsFulfilled());

  V8GCController::CollectAllGarbageForTesting(
      scope.GetIsolate(), v8::EmbedderHeapTracer::EmbedderStackState::kEmpty);

  EXPECT_FALSE(outgoing_stream);
}

TEST_F(OutgoingStreamTest, GarbageCollectionRemoteClose) {
  V8TestingScope scope;

  WeakPersistent<OutgoingStream> outgoing_stream;

  {
    v8::HandleScope handle_scope(scope.GetIsolate());

    outgoing_stream = CreateOutgoingStream(scope);
  }

  V8GCController::CollectAllGarbageForTesting(
      scope.GetIsolate(), v8::EmbedderHeapTracer::EmbedderStackState::kEmpty);

  ASSERT_TRUE(outgoing_stream);

  // Close the other end of the pipe.
  data_pipe_consumer_.reset();

  test::RunPendingTasks();

  V8GCController::CollectAllGarbageForTesting(
      scope.GetIsolate(), v8::EmbedderHeapTracer::EmbedderStackState::kEmpty);

  EXPECT_FALSE(outgoing_stream);
}

TEST_F(OutgoingStreamTest, DataPipeClosed) {
  V8TestingScope scope;

  auto* outgoing_stream = CreateOutgoingStream(scope);
  auto* script_state = scope.GetScriptState();

  ScriptPromise writing_aborted = outgoing_stream->writingAborted();
  ScriptPromiseTester writing_aborted_tester(script_state, writing_aborted);

  auto* writer =
      outgoing_stream->writable()->getWriter(script_state, ASSERT_NO_EXCEPTION);
  ScriptPromise closed = writer->closed(script_state);
  ScriptPromiseTester closed_tester(script_state, closed);

  // Close the other end of the pipe.
  data_pipe_consumer_.reset();

  writing_aborted_tester.WaitUntilSettled();
  EXPECT_TRUE(writing_aborted_tester.IsFulfilled());

  closed_tester.WaitUntilSettled();
  EXPECT_TRUE(closed_tester.IsRejected());

  DOMException* closed_exception = V8DOMException::ToImplWithTypeCheck(
      scope.GetIsolate(), closed_tester.Value().V8Value());
  ASSERT_TRUE(closed_exception);
  EXPECT_EQ(closed_exception->name(), "NetworkError");
  EXPECT_EQ(closed_exception->message(),
            "The stream was aborted by the remote server");

  auto* chunk = DOMArrayBuffer::Create('C', 1);
  ScriptPromise result =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);
  ScriptPromiseTester write_tester(script_state, result);
  write_tester.WaitUntilSettled();

  EXPECT_TRUE(write_tester.IsRejected());

  DOMException* write_exception = V8DOMException::ToImplWithTypeCheck(
      scope.GetIsolate(), write_tester.Value().V8Value());
  ASSERT_TRUE(write_exception);
  EXPECT_EQ(write_exception->name(), "NetworkError");
  EXPECT_EQ(write_exception->message(),
            "The stream was aborted by the remote server");
}

TEST_F(OutgoingStreamTest, DataPipeClosedDuringAsyncWrite) {
  V8TestingScope scope;

  constexpr uint32_t kPipeCapacity = 512 * 1024;
  auto* outgoing_stream = CreateOutgoingStream(scope, kPipeCapacity);

  auto* script_state = scope.GetScriptState();

  ScriptPromise writing_aborted = outgoing_stream->writingAborted();
  ScriptPromiseTester writing_aborted_tester(script_state, writing_aborted);

  auto* writer =
      outgoing_stream->writable()->getWriter(script_state, ASSERT_NO_EXCEPTION);

  const size_t kChunkSize = kPipeCapacity * 2;
  auto* chunk = DOMArrayBuffer::Create(kChunkSize, 1);
  ScriptPromise result =
      writer->write(script_state, ScriptValue::From(script_state, chunk),
                    ASSERT_NO_EXCEPTION);
  ScriptPromiseTester write_tester(script_state, result);

  ScriptPromise closed = writer->closed(script_state);
  ScriptPromiseTester closed_tester(script_state, closed);

  // Close the other end of the pipe.
  data_pipe_consumer_.reset();

  write_tester.WaitUntilSettled();

  EXPECT_TRUE(write_tester.IsRejected());

  DOMException* write_exception = V8DOMException::ToImplWithTypeCheck(
      scope.GetIsolate(), write_tester.Value().V8Value());
  ASSERT_TRUE(write_exception);
  EXPECT_EQ(write_exception->name(), "NetworkError");
  EXPECT_EQ(write_exception->message(),
            "The stream was aborted by the remote server");

  closed_tester.WaitUntilSettled();

  EXPECT_TRUE(closed_tester.IsRejected());

  DOMException* closed_exception = V8DOMException::ToImplWithTypeCheck(
      scope.GetIsolate(), write_tester.Value().V8Value());
  ASSERT_TRUE(closed_exception);
  EXPECT_EQ(closed_exception->name(), "NetworkError");
  EXPECT_EQ(closed_exception->message(),
            "The stream was aborted by the remote server");

  writing_aborted_tester.WaitUntilSettled();

  EXPECT_TRUE(writing_aborted_tester.IsFulfilled());
}

}  // namespace

}  // namespace blink
