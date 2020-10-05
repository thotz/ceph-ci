//
// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2020 Red Hat Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation. See file COPYING.
 *
 */
// Demonstrates basic usage of the OpenTracing API. Uses OpenTracing's
// mocktracer to capture all the recorded spans as JSON.

#ifndef TRACER_H_
#define TRACER_H_

#define SIGNED_RIGHT_SHIFT_IS 1
#define ARITHMETIC_RIGHT_SHIFT 1

#include "common/debug.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_osd
#undef dout_prefix
#define dout_prefix *_dout << "jaeger-osd "

#include <jaegertracing/Tracer.h>

typedef std::unique_ptr<opentracing::Span> jspan;

namespace jaeger_tracing{
//#ifdef HAVE_JAEGER
   static void init_tracer(const char* tracerName){
    dout(3) << "cofiguring jaegertracing" << dendl;
    static auto yaml = YAML::LoadFile("../src/jaegertracing/config.yml");
//    auto yaml = R"cfg(
//  disabled: false
//  reporter:
//      logSpans: false
//      queueSize: 100
//      bufferFlushInterval: 10
//  sampler:
//    type: const
//    param: 1
//  headers:
//      jaegerDebugHeader: debug-id
//      jaegerBaggageHeader: baggage
//      TraceContextHeaderName: trace-id
//      traceBaggageHeaderPrefix: "testctx-"
//  baggage_restrictions:
//      denyBaggageOnInitializationFailure: false
//      refreshInterval: 60
//  )cfg";
//  const auto configuration = jaegertracing::Config::parse(YAML::Load(yaml));
  dout(3) << "yaml parsed" << yaml << dendl;
  static auto configuration = jaegertracing::Config::parse(yaml);
  dout(3) << "config created" << dendl;
  auto tracer = jaegertracing::Tracer::make( tracerName, configuration,
	jaegertracing::logging::consoleLogger());
  dout(3) << "tracer_jaeger" << tracer << dendl;
  opentracing::Tracer::InitGlobal(
      std::static_pointer_cast<opentracing::Tracer>(tracer));
  dout(3) << "tracer_work" << tracer << dendl;
  auto parent_span = tracer->StartSpan("parent");
  assert(parent_span);

  parent_span->Finish();

  }

  //method to create a root jspan
     jspan new_span(const char*);

  //method to create a child_span used given parent_span
     jspan child_span(const char*, const jspan&);

  //method to finish tracing of a single jspan
     void finish_span(const jspan&);

  //setting tags in sundefined reference topans
   void set_span_tag(const jspan&, const char*, const char*);
//#else
//  typedef char jspan;
//  int* child_span(...) {return nullptr;}
//  int* new_span(...) {return nullptr;}
//  void finish_span(...) {}
//  void init_jaeger(...) {}
//  void set_span_tag(...) {}
//#endif // HAVE_JAEGER
}
#endif // TRACER_H_
