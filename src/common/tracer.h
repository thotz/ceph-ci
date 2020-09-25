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

#include <arpa/inet.h>
#include <yaml-cpp/yaml.h>
#include <jaegertracing/Tracer.h>

typedef std::unique_ptr<opentracing::Span> jspan;

namespace jaeger_tracing{

   extern std::shared_ptr<opentracing::v3::Tracer> tracer;

   extern void init_tracer(const char*);

  //method to create a root jspan
   extern jspan new_span(const char*);

  //method to create a child_span used given parent_span
   extern jspan child_span(const char*, const jspan* const);

  //method to finish tracing of a single jspan
   extern void finish_span(jspan*);

  //setting tags in sundefined reference topans
   extern void set_span_tag(jspan*, const char*, const char*);
}
#endif
