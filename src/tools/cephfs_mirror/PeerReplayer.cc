// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "common/admin_socket.h"
#include "common/ceph_context.h"
#include "common/debug.h"
#include "common/errno.h"
#include "include/stringify.h"
#include "FSMirror.h"
#include "PeerReplayer.h"
#include "Utils.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_cephfs_mirror
#undef dout_prefix
#define dout_prefix *_dout << "cephfs::mirror::PeerReplayer("   \
                           << m_peer.uuid << ") " << __func__

namespace cephfs {
namespace mirror {

namespace {

class PeerAdminSocketCommand {
public:
  virtual ~PeerAdminSocketCommand() {
  }
  virtual int call(Formatter *f) = 0;
};

class StatusCommand : public PeerAdminSocketCommand {
public:
  explicit StatusCommand(PeerReplayer *peer_replayer)
    : peer_replayer(peer_replayer) {
  }

  int call(Formatter *f) override {
    peer_replayer->peer_status(f);
    return 0;
  }

private:
  PeerReplayer *peer_replayer;
};

} // anonymous namespace

class PeerReplayerAdminSocketHook : public AdminSocketHook {
public:
  PeerReplayerAdminSocketHook(CephContext *cct, const Filesystem &filesystem,
                              const Peer &peer, PeerReplayer *peer_replayer)
    : admin_socket(cct->get_admin_socket()) {
    int r;
    std::string cmd;

    // mirror peer status format is name@id uuid
    cmd = "fs mirror peer status "
          + stringify(filesystem.fs_name) + "@" + stringify(filesystem.fscid)
          + " "
          + stringify(peer.uuid);
    r = admin_socket->register_command(
      cmd, this, "get peer mirror status");
    if (r == 0) {
      commands[cmd] = new StatusCommand(peer_replayer);
    }
  }

  ~PeerReplayerAdminSocketHook() override {
    admin_socket->unregister_commands(this);
    for (auto &[command, cmdptr] : commands) {
      delete cmdptr;
    }
  }

  int call(std::string_view command, const cmdmap_t& cmdmap,
           Formatter *f, std::ostream &errss, bufferlist &out) override {
    auto p = commands.at(std::string(command));
    return p->call(f);
  }

private:
  typedef std::map<std::string, PeerAdminSocketCommand*, std::less<>> Commands;

  AdminSocket *admin_socket;
  Commands commands;
};

PeerReplayer::PeerReplayer(CephContext *cct, FSMirror *fs_mirror,
                           const Filesystem &filesystem, const Peer &peer,
                           const std::set<std::string, std::less<>> &directories,
                           MountRef mount)
  : m_fs_mirror(fs_mirror),
    m_peer(peer),
    m_directories(directories),
    m_local_mount(mount),
    m_asok_hook(new PeerReplayerAdminSocketHook(cct, filesystem, peer, this)),
    m_lock(ceph::make_mutex("cephfs::mirror::PeerReplayer::" + stringify(peer.uuid))) {
}

PeerReplayer::~PeerReplayer() {
  delete m_asok_hook;
}

int PeerReplayer::init() {
  dout(20) << ": initial dir list=[" << m_directories << "]" << dendl;

  auto &remote_client = m_peer.remote.client_name;
  auto &remote_cluster = m_peer.remote.cluster_name;
  auto remote_filesystem = Filesystem{0, m_peer.remote.fs_name};

  int r = connect(remote_client, remote_cluster, &m_remote_cluster);
  if (r < 0) {
    derr << ": error connecting to remote cluster: " << cpp_strerror(r)
         << dendl;
    return r;
  }

  r = mount(m_remote_cluster, remote_filesystem, false, &m_remote_mount);
  if (r < 0) {
    m_remote_cluster.reset();
    derr << ": error mounting remote filesystem=" << remote_filesystem << dendl;
    return r;
  }

  std::scoped_lock locker(m_lock);
  auto nr_replayers = g_ceph_context->_conf.get_val<uint64_t>(
    "cephfs_mirror_max_concurrent_directory_syncs");
  dout(20) << ": spawning " << nr_replayers << " snapshot replayer(s)" << dendl;

  while (nr_replayers-- > 0) {
    std::unique_ptr<SnapshotReplayerThread> replayer(
      new SnapshotReplayerThread(this));
    std::string name("replayer-" + stringify(nr_replayers));
    replayer->create(name.c_str());
    m_replayers.push_back(std::move(replayer));
  }

  return 0;
}

void PeerReplayer::shutdown() {
  dout(20) << dendl;

  {
    std::scoped_lock locker(m_lock);
    ceph_assert(!m_stopping);
    m_stopping = true;
    m_cond.notify_all();
  }

  for (auto &replayer : m_replayers) {
    replayer->join();
  }
  m_replayers.clear();
  ceph_unmount(m_remote_mount);
  m_remote_cluster.reset();
}

void PeerReplayer::add_directory(string_view dir_path) {
  dout(20) << ": dir_path=" << dir_path << dendl;

  std::scoped_lock locker(m_lock);
  m_directories.emplace(dir_path);
  m_cond.notify_all();
}

void PeerReplayer::remove_directory(string_view dir_path) {
  dout(20) << ": dir_path=" << dir_path << dendl;

  std::scoped_lock locker(m_lock);
  m_directories.erase(std::string(dir_path));
  m_cond.notify_all();
}

void PeerReplayer::run() {
  std::unique_lock locker(m_lock);
  while (true) {
    dout(20) << ": trying to pick from " << m_directories.size() << " directories" << dendl;
    // do not check if client is blocklisted under lock
    m_cond.wait_for(locker, 1s, [this]{return m_directories.size() || is_stopping();});
    if (is_stopping()) {
      dout(5) << ": exiting" << dendl;
      break;
    }

    locker.unlock();

    if (m_fs_mirror->is_blocklisted()) {
      dout(5) << ": exiting as client is blocklisted" << dendl;
      break;
    }

    struct stat stbuf;
    int r = ceph_stat(m_local_mount, "/", &stbuf);
    if (r < 0) {
      derr << ": failed to stat root:" << cpp_strerror(r) << dendl;
    } else {
      dout(0) << ": root ino=" << stbuf.st_ino << dendl;
    }

    ::sleep(1);

    locker.lock();
  }
}

void PeerReplayer::peer_status(Formatter *f) {
}

} // namespace mirror
} // namespace cephfs
