// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPHFS_MIRROR_PEER_REPLAYER_H
#define CEPHFS_MIRROR_PEER_REPLAYER_H

#include "common/Formatter.h"
#include "common/Thread.h"
#include "mds/FSMap.h"
#include "Types.h"

class CephContext;

namespace cephfs {
namespace mirror {

class FSMirror;
class PeerReplayerAdminSocketHook;

class PeerReplayer {
public:
  PeerReplayer(CephContext *cct, FSMirror *fs_mirror,
               const Filesystem &filesystem, const Peer &peer,
               const std::set<std::string, std::less<>> &directories,
               MountRef mount);
  ~PeerReplayer();

  // initialize replayer for a peer
  int init();

  // shutdown replayer for a peer
  void shutdown();

  // add a directory to mirror queue
  void add_directory(string_view dir_path);

  // remove a directory from queue
  void remove_directory(string_view dir_path);

  // admin socket helpers
  void peer_status(Formatter *f);

private:
  bool is_stopping() {
    return m_stopping;
  }

  struct Replayer;
  class SnapshotReplayerThread : public Thread {
  public:
    SnapshotReplayerThread(PeerReplayer *peer_replayer)
      : m_peer_replayer(peer_replayer) {
    }

    void *entry() override {
      m_peer_replayer->run();
      return 0;
    }

  private:
    PeerReplayer *m_peer_replayer;
  };

  typedef std::vector<std::unique_ptr<SnapshotReplayerThread>> SnapshotReplayers;

  FSMirror *m_fs_mirror;
  Peer m_peer;
  std::set<std::string, std::less<>> m_directories;
  MountRef m_local_mount;
  PeerReplayerAdminSocketHook *m_asok_hook = nullptr;

  ceph::mutex m_lock;
  ceph::condition_variable m_cond;
  RadosRef m_remote_cluster;
  MountRef m_remote_mount;
  bool m_stopping = false;
  SnapshotReplayers m_replayers;

  void run();
};

} // namespace mirror
} // namespace cephfs

#endif // CEPHFS_MIRROR_PEER_REPLAYER_H
