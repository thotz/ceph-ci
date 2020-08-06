// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include <map>
#include <vector>

#include "Compressor.h"
#include "common/ceph_mutex.h"
#include "common/ceph_context.h"
#include "common/config_cacher.h"

class CompressorRegistry : public md_config_obs_t {
  CephContext *cct;
  mutable ceph::mutex lock = ceph::make_mutex("CompressorRegistry::lock");

  uint32_t ms_osd_compress_mode;
  bool ms_compress_secure;
  std::uint64_t ms_osd_compress_min_size;
  std::vector<uint32_t> ms_osd_compression_methods;

  void _refresh_config();
  void _parse_method_list(const string& s,std::vector<uint32_t> *v);

public:
  CompressorRegistry(CephContext *cct);
  ~CompressorRegistry();

  void refresh_config() {
    std::scoped_lock l(lock);
    _refresh_config();
  }

  const char** get_tracked_conf_keys() const override;
  void handle_conf_change(const ConfigProxy& conf,
                          const std::set<std::string>& changed) override;

  uint32_t pick_method(uint32_t peer_type, uint32_t comp_mode, const std::vector<uint32_t>& preferred_methods);

  const uint32_t get_mode(uint32_t peer_type, bool is_secure);

  const std::vector<uint32_t> get_methods(uint32_t peer_type) { 
    std::scoped_lock l(lock);
    switch (peer_type) {
      case CEPH_ENTITY_TYPE_OSD:
        return ms_osd_compression_methods;
      default:
        return std::vector<uint32_t>();
    }
   }

  const uint64_t get_min_compression_size(uint32_t peer_type) {
    std::scoped_lock l(lock);
    switch (peer_type) {
      case CEPH_ENTITY_TYPE_OSD:
        return ms_osd_compress_min_size;
      default:
        return 0;
    }
  }

  const bool get_is_compress_secure() { return ms_compress_secure; }
};

