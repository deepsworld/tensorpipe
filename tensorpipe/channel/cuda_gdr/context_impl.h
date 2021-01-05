/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <tensorpipe/channel/context_impl_boilerplate.h>
#include <tensorpipe/channel/cuda_context.h>
#include <tensorpipe/channel/cuda_gdr/constants.h>
#include <tensorpipe/common/busy_polling_loop.h>
#include <tensorpipe/common/cuda.h>
#include <tensorpipe/common/cuda_buffer.h>
#include <tensorpipe/common/error.h>
#include <tensorpipe/common/ibv.h>
#include <tensorpipe/transport/context.h>

namespace tensorpipe {
namespace channel {
namespace cuda_gdr {

class ChannelImpl;

class IbvNic {
 public:
  IbvNic(
      std::string id,
      std::string name,
      IbvLib::device& device,
      IbvLib& ibvLib);

  IbvProtectionDomain& getIbvPd() {
    return pd_;
  }

  IbvCompletionQueue& getIbvCq() {
    return cq_;
  }

  const IbvAddress& getIbvAddress() {
    return addr_;
  }

  void postSend(
      IbvQueuePair& qp,
      IbvLib::send_wr& wr,
      std::function<void(const Error&)> cb);

  void postRecv(
      IbvQueuePair& qp,
      IbvLib::recv_wr& wr,
      std::function<void(const Error&)> cb);

  bool pollOnce();

  IbvMemoryRegion& registerMemory(CudaBuffer buffer);

  bool readyToClose() const;

  void setId(std::string id);

 private:
  // The ID of the context, for use in verbose logging.
  std::string id_{"N/A"};
  // The name of the InfiniBand device.
  std::string name_;

  IbvLib& ibvLib_;
  IbvContext ctx_;
  IbvProtectionDomain pd_;
  IbvCompletionQueue cq_;
  IbvAddress addr_;

  size_t numAvailableRecvSlots_ = kNumRecvs;
  std::deque<std::tuple<
      IbvQueuePair&,
      IbvLib::recv_wr&,
      std::function<void(const Error&)>>>
      recvsWaitingForSlots_;

  size_t numAvailableSendSlots_ = kNumSends;
  std::deque<std::tuple<
      IbvQueuePair&,
      IbvLib::send_wr&,
      std::function<void(const Error&)>>>
      sendsWaitingForSlots_;

  // We need one common map for both send and recv requests because in principle
  // we cannot access the opcode of a failed operation, meaning we couldn't
  // match it to its callback. However, we could group them by QP number or, in
  // fact, we could have the QP store these requests and we just wake it up when
  // a completion occurs.
  std::unordered_map<uint64_t, std::function<void(const Error&)>>
      requestsInFlight_;
  uint64_t nextRequestId_ = 0;

  std::map<std::tuple<uintptr_t, size_t>, IbvMemoryRegion> memoryRegions_;
};

class ContextImpl final
    : public BusyPollingLoop,
      public ContextImplBoilerplate<CudaBuffer, ContextImpl, ChannelImpl> {
 public:
  explicit ContextImpl(std::vector<std::string> gpuIdxToNicName);

  std::shared_ptr<CudaChannel> createChannel(
      std::shared_ptr<transport::Connection> connection,
      Endpoint endpoint);

  const std::vector<size_t>& getGpuToNicMapping();

  IbvLib& getIbvLib();

  IbvNic& getIbvNic(size_t nicIdx);

  void waitForCudaEvent(
      const CudaEvent& event,
      std::function<void(const Error&)> cb);

 protected:
  // Implement BusyPollingLoop hooks.
  bool pollOnce() override;
  bool readyToClose() override;

  // Implement the entry points called by ContextImplBoilerplate.
  void closeImpl() override;
  void joinImpl() override;
  void setIdImpl() override;

 private:
  bool foundIbvLib_{false};
  IbvLib ibvLib_;
  std::vector<IbvNic> ibvNics_;

  std::vector<size_t> gpuToNic_;

  std::list<std::tuple<const CudaEvent&, std::function<void(const Error&)>>>
      pendingCudaEvents_;

  bool pollCudaOnce();

  void waitForCudaEventFromLoop(
      const CudaEvent& event,
      std::function<void(const Error&)> cb);
};

} // namespace cuda_gdr
} // namespace channel
} // namespace tensorpipe
