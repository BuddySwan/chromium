// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUCHSIA_BASE_MESSAGE_PORT_H_
#define FUCHSIA_BASE_MESSAGE_PORT_H_

#include <fuchsia/web/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <memory>

#include "base/containers/circular_deque.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/connector.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "third_party/blink/public/common/messaging/web_message_port.h"

namespace cr_fuchsia {

// TODO(crbug.com/803242): Once all downstream dependencies have migrated to
// using blink::WebMessagePort variants of this API, remove the mojo variants.

// Creates a connected MessagePort from a FIDL MessagePort request and
// returns a handle to its peer Mojo pipe.
mojo::ScopedMessagePipeHandle MessagePortFromFidl(
    fidl::InterfaceRequest<fuchsia::web::MessagePort> fidl_port);

// Creates a connected MessagePort from a remote FIDL MessagePort handle,
// returns a handle to its peer Mojo pipe.
mojo::ScopedMessagePipeHandle MessagePortFromFidl(
    fidl::InterfaceHandle<fuchsia::web::MessagePort> fidl_port);

// Creates a connected MessagePort from a transferred Mojo MessagePort and
// returns a handle to its FIDL interface peer.
fidl::InterfaceHandle<fuchsia::web::MessagePort> MessagePortFromMojo(
    mojo::ScopedMessagePipeHandle mojo_port);

// Creates a connected MessagePort from a FIDL MessagePort request and
// returns a handle to its peer blink::WebMessagePort.
blink::WebMessagePort BlinkMessagePortFromFidl(
    fidl::InterfaceRequest<fuchsia::web::MessagePort> fidl_port);

// Creates a connected MessagePort from a remote FIDL MessagePort handle,
// returns a handle to its peer Mojo pipe.
blink::WebMessagePort BlinkMessagePortFromFidl(
    fidl::InterfaceHandle<fuchsia::web::MessagePort> fidl_port);

// Creates a connected MessagePort from a transferred blink::WebMessagePort and
// returns a handle to its FIDL interface peer.
fidl::InterfaceHandle<fuchsia::web::MessagePort> FidlMessagePortFromBlink(
    blink::WebMessagePort blink_port);

}  // namespace cr_fuchsia

#endif  // FUCHSIA_BASE_MESSAGE_PORT_H_
