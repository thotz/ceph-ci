#include <vector>

#include <string.h>

#include "common/ceph_argparse.h"
#include "common/config.h"
#include "common/debug.h"
#include "common/errno.h"
#include "common/async/context_pool.h"
#include "global/global_init.h"
#include "global/signal_handler.h"
#include "mon/MonClient.h"
#include "msg/Messenger.h"

#include "SnapReplicator.h"

using namespace std::literals::string_literals;

enum snap_cluster_type_t {
  SOURCE = 1,
  DEST   = 2
};

class SnapReplicatorDispatcher
{
  public:
    SnapReplicatorDispatcher(const std::string& _src_conf_path,
                             const std::string& _dst_conf_path,
                             const std::string& _src_dir,
                             const std::string& _src_fs_name,
                             const std::string& _dst_fs_name,
                             const std::string& _old_snap_name,
                             const std::string& _new_snap_name,
                             const std::string& _src_auth_id,
                             const std::string& _dst_auth_id) :
      src_conf_path(_src_conf_path),
      dst_conf_path(_dst_conf_path),
      src_dir(_src_dir),
      src_fs_name(_src_fs_name),
      dst_fs_name(_dst_fs_name),
      old_snap_name(_old_snap_name),
      new_snap_name(_new_snap_name),
      src_auth_id(_src_auth_id),
      dst_auth_id(_dst_auth_id)
    { }

    ~SnapReplicatorDispatcher()
    {
    }

    int dispatch();

    int finish_replication();

    int get_errno() {
      return my_errno;
    }

  private:
    const std::string src_conf_path;
    const std::string dst_conf_path;
    const std::string src_dir;      // dir for which snapshots are being replicated
    const std::string src_fs_name;
    const std::string dst_fs_name;
    const std::string old_snap_name;
    const std::string new_snap_name;
    const std::string src_auth_id;
    const std::string dst_auth_id;
    std::string src_snap_dir;     // ".snap" or something user configured
    std::string dst_snap_dir;     // ".snap" or something user configured

    int my_errno = 0;
    bool is_src_mounted = false;
    bool is_dst_mounted = false;
    struct ceph_mount_info *src_mnt = nullptr;
    struct ceph_mount_info *dst_mnt = nullptr;

    int connect_to_cluster(snap_cluster_type_t sct);
    int create_directory(struct ceph_mount_info *mnt, const std::string& dir);
};

int SnapReplicatorDispatcher::create_directory(struct ceph_mount_info *mnt, const std::string& dir)
{
  int rv = ceph_mkdirs(mnt, dir.c_str(), 0755);
  if (rv < 0 && -rv != EEXIST) {
    my_errno = -rv;
    return -1;
  }
  return 0;
}

int SnapReplicatorDispatcher::connect_to_cluster(snap_cluster_type_t sct)
{
  ceph_assert(sct == snap_cluster_type_t::SOURCE || sct == snap_cluster_type_t::DEST);

  struct ceph_mount_info *& mnt = (sct == SOURCE ? src_mnt        : dst_mnt);
  const std::string& conf_path  = (sct == SOURCE ? src_conf_path  : dst_conf_path);
  const std::string& fs_name    = (sct == SOURCE ? src_fs_name    : dst_fs_name);
  std::string& snap_dir         = (sct == SOURCE ? src_snap_dir   : dst_snap_dir);
  bool& is_mounted              = (sct == SOURCE ? is_src_mounted : is_dst_mounted);
  const std::string& auth_id    = (sct == SOURCE ? src_auth_id    : dst_auth_id);

  int rv = ceph_create(&mnt, auth_id.c_str());
  if (rv < 0) {
    std::clog << ": ceph_create failed!\n";
    my_errno = -rv;
    return -1;
  }
  std::clog << __func__ << ":" << __LINE__ << ": created mount\n";

  rv = ceph_conf_read_file(mnt, conf_path.c_str());
  if (rv < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ceph_conf_read_file cluster conf failed!\n";
    my_errno = -rv;
    return -1;
  }
  std::clog << __func__ << ":" << __LINE__ << ": conf file read completed\n";

  rv = ceph_conf_parse_env(mnt, NULL);
  if (rv < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ceph_conf_parse_env failed!\n";
    my_errno = -rv;
    return -1;
  }
  std::clog << __func__ << ":" << __LINE__ << ": parse env completed\n";
#if 0
  rv = ceph_init(mnt);
  if (rv < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ceph_init failed!\n";
    my_errno = -rv;
    return -1;
  }
  std::clog << __func__ << ":" << __LINE__ << ": init completed\n";
#endif
  char buf[128] = {0,};
  rv = ceph_conf_get(mnt, "client_snapdir", buf, sizeof(buf));
  if (rv < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ceph_conf_get client_snapdir failed!\n";
    my_errno = -rv;
    return -1;
  }
  snap_dir = buf;
  std::clog << __func__ << ":" << __LINE__ << ": client_snapdir is '" << snap_dir << "'\n";

  rv = ceph_select_filesystem(mnt, fs_name.c_str());
  if (rv < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ceph_select_filesystem failed!\n";
    my_errno = -rv;
    return -1;
  }
  std::clog << __func__ << ":" << __LINE__ << ": filesystem '" << fs_name << "' selected\n";

  // we mount the root; then create the snap root dir; and unmount the root dir
  rv = ceph_mount(mnt, "/");
  if (rv < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ceph_mount root failed!\n";
    my_errno = -rv;
    return -1;
  }
  std::clog << __func__ << ":" << __LINE__ << ": filesystem '/' dir mounted\n";

  ceph_assert(ceph_is_mounted(mnt));

  if (sct == snap_cluster_type_t::DEST) {
    rv = create_directory(mnt, src_dir);
    if (rv < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": error creating snapshot destination dir '" << src_dir << "'\n";
      return -1;
    }
    std::clog << __func__ << ":" << __LINE__ << ": '" << src_dir << "' created\n";
  }
  is_mounted = true;

  return 0;
}

int SnapReplicatorDispatcher::finish_replication()
{
  int rv = 0;

  if (is_dst_mounted) {
    rv = ceph_sync_fs(dst_mnt);
    ceph_unmount(dst_mnt);
    ceph_release(dst_mnt);
    dst_mnt = nullptr;
    is_dst_mounted = false;
  }

  if (is_src_mounted) {
    ceph_unmount(src_mnt);
    ceph_release(src_mnt);
    src_mnt = nullptr;
    is_src_mounted = false;
  }

  return rv;
}

int SnapReplicatorDispatcher::dispatch()
{
  std::clog << __func__ << ":" << __LINE__ << ": snap-replicator created\n";

  std::clog << __func__ << ":" << __LINE__ << ": snap-replicator connecting to source cluster\n";

  if (connect_to_cluster(snap_cluster_type_t::SOURCE) < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ERROR: failed to connect to source cluster (" << get_errno() << ":" << strerror(get_errno()) << ")\n";
    return 1;
  }

  std::clog << __func__ << ":" << __LINE__ << ": snap-replicator connected to source cluster\n";
  std::clog << __func__ << ":" << __LINE__ << ": \n\n";
  std::clog << __func__ << ":" << __LINE__ << ": snap-replicator connecting to destination cluster\n";
  
  if (connect_to_cluster(snap_cluster_type_t::DEST) < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ERROR: failed to connect to destination cluster (" << get_errno() << ":" << strerror(get_errno()) << ")\n";
    return 1;
  }
  
  std::clog << __func__ << ":" << __LINE__ << ": snap-replicator connected to destination cluster\n";
  std::clog << __func__ << ":" << __LINE__ << ": \n\n";

  SnapReplicator sr(src_mnt, dst_mnt, src_dir, std::make_pair(old_snap_name, new_snap_name));

  if (sr.replicate() < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": snap-replicator replicate failed\n";
    return 1;
  }
  
  std::clog << __func__ << ":" << __LINE__ << ": snap-replicator replicate done\n";
  
  if (finish_replication() < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": ERROR: finishing replication\n";
    return 1;
  }
  
  std::clog << __func__ << ":" << __LINE__ << ": snap-replicator finish_replication done\n";
  return 0;
}

void usage(const char **argv)
{
  std::cerr << "Usage:\n\t";
  std::cerr << argv[0] << " <old_snap_name> <new_snap_name>\n";
}

int main(int argc, const char **argv)
{
  if (argc < 10) {
    std::cerr << "Usage:\n\t";
    std::cerr << argv[0] << " <src conf> <dst conf> <src dir> <src fs> <dst fs> <src auth id> <dst auth id> <old_snap_name> <new_snap_name>\n";
    ::exit(0);
  }

  // const std::string src_conf = "/etc/ceph/src.conf";
  // const std::string dst_conf = "/etc/ceph/dst.conf";
  // const std::string src_path = "/mchangir";
  // const std::string src_fs_name = "a";
  // const std::string dst_fs_name = "a";
  // const std::string src_auth_id = "fs_a";
  // const std::string dst_auth_id   = "admin";
  // const std::string old_snap_name  = argv[1];
  // const std::string new_snap_name  = argv[2];

  const std::string src_conf = argv[0];
  const std::string dst_conf = argv[1];
  const std::string src_path = argv[2];
  const std::string src_fs_name = argv[3];
  const std::string dst_fs_name = argv[4];
  const std::string src_auth_id = argv[5];
  const std::string dst_auth_id   = argv[6];
  const std::string old_snap_name  = argv[7];
  const std::string new_snap_name  = argv[8];

  SnapReplicatorDispatcher srd(src_conf, dst_conf,
                               src_path, src_fs_name, dst_fs_name,
                               old_snap_name, new_snap_name,
                               src_auth_id, dst_auth_id);

  srd.dispatch();

  return 0;
}
