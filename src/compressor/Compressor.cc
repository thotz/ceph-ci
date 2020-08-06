// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 *
 */

#include <random>
#include <sstream>
#include <iterator>
#include <algorithm>

#include "CompressionPlugin.h"
#include "Compressor.h"
#include "include/random.h"
#include "common/ceph_context.h"
#include "common/debug.h"
#include "common/dout.h"

CompressorRef Compressor::create(CephContext *cct, const std::string &type)
{
  // support "random" for teuthology testing
  if (type == "random") {
    int alg = ceph::util::generate_random_number(0, COMP_ALG_LAST - 1);
    if (alg == COMP_ALG_NONE) {
      return nullptr;
    }
    return create(cct, alg);
  }

  CompressorRef cs_impl = NULL;
  std::stringstream ss;
  auto reg = cct->get_plugin_registry();
  auto factory = dynamic_cast<ceph::CompressionPlugin*>(reg->get_with_load("compressor", type));
  if (factory == NULL) {
    lderr(cct) << __func__ << " cannot load compressor of type " << type << dendl;
    return NULL;
  }
  int err = factory->factory(&cs_impl, &ss);
  if (err)
    lderr(cct) << __func__ << " factory return error " << err << dendl;
  return cs_impl;
}

CompressorRef Compressor::create(CephContext *cct, int alg)
{
  if (alg < 0 || alg >= COMP_ALG_LAST) {
    lderr(cct) << __func__ << " invalid algorithm value:" << alg << dendl;
    return CompressorRef();
  }
  std::string type_name = get_comp_alg_name(alg);
  return create(cct, type_name);
}
