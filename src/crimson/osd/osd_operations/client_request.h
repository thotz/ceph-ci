// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include "osd/osd_op_util.h"
#include "crimson/net/Connection.h"
#include "crimson/osd/osd_operation.h"
#include "crimson/common/type_helpers.h"
#include "messages/MOSDOp.h"

namespace crimson::osd {
class PG;
class OSD;

class ClientRequest final : public OperationT<ClientRequest> {
  OSD &osd;
  crimson::net::ConnectionRef conn;
  Ref<MOSDOp> m;
  OpInfo op_info;
  OrderedPipelinePhase::Handle handle;
  ObjectContextRef object_context_ref;

public:
  class ConnectionPipeline {
    OrderedPipelinePhase await_map = {
      "ClientRequest::ConnectionPipeline::await_map"
    };
    OrderedPipelinePhase get_pg = {
      "ClientRequest::ConnectionPipeline::get_pg"
    };
    friend class ClientRequest;
  };
  class PGPipeline {
    OrderedPipelinePhase await_map = {
      "ClientRequest::PGPipeline::await_map"
    };
    OrderedPipelinePhase wait_for_active = {
      "ClientRequest::PGPipeline::wait_for_active"
    };
    OrderedPipelinePhase recover_missing = {
      "ClientRequest::PGPipeline::recover_missing"
    };
    OrderedPipelinePhase get_obc = {
      "ClientRequest::PGPipeline::get_obc"
    };
    OrderedPipelinePhase process = {
      "ClientRequest::PGPipeline::process"
    };
    friend class ClientRequest;
  };

  static constexpr OperationTypeCode type = OperationTypeCode::client_request;

  ClientRequest(OSD &osd, crimson::net::ConnectionRef, Ref<MOSDOp> &&m);

  void print(std::ostream &) const final;
  void dump_detail(Formatter *f) const final;

public:
  seastar::future<> start();

private:
  ::crimson::interruptible::interruptible_future<
    ::crimson::osd::IOInterruptCondition> process_pg_op(
    Ref<PG> &pg);
  ::crimson::interruptible::interruptible_future<
    ::crimson::osd::IOInterruptCondition> process_op(
    Ref<PG> &pg);
  bool is_pg_op() const;

  ConnectionPipeline &cp();
  PGPipeline &pp(PG &pg);

  OperationRepeatSequencer<ClientRequest>& ors;
private:
  bool is_misdirected(const PG& pg) const;
};

}
