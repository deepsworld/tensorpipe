/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/transport/uv/listener.h>

#include <tensorpipe/common/callback.h>
#include <tensorpipe/transport/error_macros.h>
#include <tensorpipe/transport/uv/connection.h>
#include <tensorpipe/transport/uv/error.h>
#include <tensorpipe/transport/uv/uv.h>

namespace tensorpipe {
namespace transport {
namespace uv {

std::shared_ptr<Listener> Listener::create(
    std::shared_ptr<Loop> loop,
    const Sockaddr& addr) {
  auto listener =
      std::make_shared<Listener>(ConstructorToken(), std::move(loop), addr);
  listener->start();
  return listener;
}

Listener::Listener(
    ConstructorToken /* unused */,
    std::shared_ptr<Loop> loop,
    const Sockaddr& addr)
    : loop_(std::move(loop)) {
  listener_ = loop_->createHandle<TCPHandle>();
  listener_->bind(addr);
}

Listener::~Listener() {
  if (listener_) {
    listener_->close();
  }
}

void Listener::start() {
  listener_->listen(runIfAlive(
      *this,
      std::function<void(Listener&, int)>([](Listener& listener, int status) {
        listener.connectionCallback(status);
      })));
}

Sockaddr Listener::sockaddr() {
  return listener_->sockName();
}

void Listener::accept(accept_callback_fn fn) {
  callback_.arm(std::move(fn));
}

address_t Listener::addr() const {
  return listener_->sockName().str();
}

void Listener::connectionCallback(int status) {
  if (status != 0) {
    callback_.trigger(
        TP_CREATE_ERROR(UVError, status), std::shared_ptr<Connection>());
    return;
  }

  auto connection = loop_->createHandle<TCPHandle>();
  listener_->accept(connection);
  callback_.trigger(
      Error::kSuccess, Connection::create(loop_, std::move(connection)));
}

} // namespace uv
} // namespace transport
} // namespace tensorpipe
