#include <dirent.h>
#include <string.h>
#include <limits.h>

#include "SnapReplicator.h"

using namespace std::literals::string_literals;

bool operator ==(const struct timespec& lhs, const struct timespec& rhs);
bool operator >(const struct timespec& lhs, const struct timespec& rhs);
bool operator >=(const struct timespec& lhs, const struct timespec& rhs);


SnapReplicator::SnapReplicator(struct ceph_mount_info *_src_mnt,
                               struct ceph_mount_info *_dst_mnt,
                               const std::string& _src_dir,
                               const std::pair<std::string, std::string>& snaps)
  : m_src_mnt(_src_mnt),
    m_dst_mnt(_dst_mnt),
    m_src_dir(_src_dir),
    m_old_snap(snaps.first),
    m_new_snap(snaps.second)
{
  char buf[128] = {0,};
  if (ceph_conf_get(m_src_mnt, "client_snapdir", buf, sizeof(buf)) < 0)
    std::clog << __func__ << ":" << __LINE__ << ": unable to get src client_snapdir" << std::endl;
  else
    m_src_snap_dir = buf;
  if (ceph_conf_get(m_dst_mnt, "client_snapdir", buf, sizeof(buf)) < 0)
    std::clog << __func__ << ":" << __LINE__ << ": unable to get dst client_snapdir" << std::endl;
  else
    m_dst_snap_dir = buf;
}

int SnapReplicator::handle_old_entry(const std::string& dir_name,
                                     const std::string& old_dentry,
                                     const struct dirent& de,
                                     const struct ceph_statx& old_stx,
                                     const struct ceph_statx& new_stx,
                                     const struct timespec& old_rctime,
                                     std::queue<std::string>& dir_queue)
{
  std::string entry_path = dir_name + "/"s + de.d_name;
  if (S_ISDIR(new_stx.stx_mode)) {
    dir_queue.push(entry_path);
  } else if (S_ISREG(new_stx.stx_mode)) {
    if (new_stx.stx_mtime > old_stx.stx_mtime or new_stx.stx_ctime > old_stx.stx_ctime) {
      if (copy_blocks(entry_path, file_type_t::OLD_FILE, old_stx, new_stx) < 0) {
        std::clog << __func__ << ":" << __LINE__ << ":  failed to copy blocks for " << old_dentry << std::endl;
        return -1;
      }

      std::string dst_dentry_path = m_src_dir + "/"s + entry_path;
      if (new_stx.stx_size != old_stx.stx_size) {
        if (ceph_truncate(m_dst_mnt, dst_dentry_path.c_str(), new_stx.stx_size) < 0) {
          std::clog << __func__ << ":" << __LINE__ << ":  failed to trundate to " << new_stx.stx_size << std::endl;
          return -1;
        }
      }
      if (new_stx.stx_uid != old_stx.stx_uid or new_stx.stx_gid != old_stx.stx_gid) {
        if (ceph_lchown(m_dst_mnt, dst_dentry_path.c_str(), new_stx.stx_uid, new_stx.stx_gid) < 0) {
          std::clog << __func__ << ":" << __LINE__ << ":  failed to lchown(uid:" << new_stx.stx_uid << ", gid:" << new_stx.stx_gid << ")" << std::endl;
          return -1;
        }
      }
    }
  }
  return 0;
}

int SnapReplicator::handle_new_entry(const std::string& dst_entry_path,
                                     const std::string& dentry,
                                     const struct dirent& de,
                                     const struct ceph_statx& new_stx,
                                     std::queue<std::string>& dir_queue)
{
  int rv = 0;

  if (S_ISDIR(new_stx.stx_mode)) {
    if ((rv = ceph_mkdir(m_dst_mnt, dst_entry_path.c_str(), new_stx.stx_mode)) == 0) {
      std::clog << __func__ << ":" << __LINE__ << ":  created dir " << dst_entry_path << std::endl;
      std::clog << __func__ << ":" << __LINE__ << ":  queueing dentry " << dentry << std::endl;
      dir_queue.push(dentry);
    } else {
      std::clog << __func__ << ":" << __LINE__ << ":  failed to create dir "
                << dst_entry_path << " (" << strerror(-rv) << ")" << std::endl;
      my_errno = -rv;
      return -1;
    }
  } else if (S_ISREG(new_stx.stx_mode)) {
    struct ceph_statx old_stx{0,};
    if ((rv = copy_blocks(dentry, file_type_t::NEW_FILE, old_stx, new_stx)) == 0) {
      if ((rv = ceph_truncate(m_dst_mnt, dst_entry_path.c_str(), new_stx.stx_size)) == 0) {
        if ((rv = ceph_lchown(m_dst_mnt, dst_entry_path.c_str(), new_stx.stx_uid, new_stx.stx_gid)) < 0) {
          std::clog << __func__ << ":" << __LINE__ << ":  failed to lchown(uid:" << new_stx.stx_uid << ", gid:" << new_stx.stx_gid << ")" << std::endl;
          my_errno = -rv;
          return -1;
        }
      } else {
        std::clog << __func__ << ":" << __LINE__ << ":  failed to trundate to " << new_stx.stx_size << std::endl;
        my_errno = -rv;
        return -1;
      }
    } else {
      std::clog << __func__ << ":" << __LINE__ << ":  failed to copy blocks for " << dst_entry_path << std::endl;
      my_errno = -rv;
      return -1;
    }
  } else if (S_ISLNK(new_stx.stx_mode)) {
    char buf[PATH_MAX+1] = {0,};
    if ((rv = ceph_readlink(m_src_mnt, dentry.c_str(), buf, sizeof(buf))) > 0) {
      if ((rv = ceph_symlink(m_dst_mnt, buf, dst_entry_path.c_str())) < 0) {
        std::clog << __func__ << ":" << __LINE__ << ":  failed to symlink(" << buf << ", " << dst_entry_path << ")" << std::endl;
        my_errno = -rv;
        return -1;
      }
    } else {
      std::clog << __func__ << ":" << __LINE__ << ":  failed to readlink(" << dentry << ")" << std::endl;
      my_errno = -rv;
      return -1;
    }
  } else {
    std::clog << __func__ << ":" << __LINE__ << ":  unhandled entry " << de.d_name << std::endl;
  }
  return 0;
}

int SnapReplicator::replicate()
{
  bool is_error = false;
  const struct timespec epoch_rctime{0,0};
  const struct timespec old_rctime = get_rctime(m_old_snap, m_src_dir + "/"s + m_src_snap_dir + "/"s + m_old_snap);
  std::string new_snap_root = m_src_dir + "/"s + m_src_snap_dir + "/"s + m_new_snap;
  struct ceph_statx old_stx;
  struct ceph_statx new_stx;
  struct ceph_dir_result *new_dirp = NULL;
  struct dirent de;
  bool is_first_sync = (old_rctime == epoch_rctime);
  std::queue<std::string> dir_queue;

  std::clog << __func__ << ":" << __LINE__ << ": old rctime:"
    << old_rctime.tv_sec << "."
    << std::setw(9) << std::setfill('0')
    << old_rctime.tv_nsec
    << std::endl;

  if (ceph_chdir(m_src_mnt, new_snap_root.c_str()) < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": unable to chdir to " << new_snap_root << std::endl;
    return -1;
  } else {
    std::clog << __func__ << ":" << __LINE__ << ": chdir to " << new_snap_root << std::endl;
  }

  dir_queue.push(""s);
  // replicate the changes
  // we traverse the new snap only in dirs with (mtime|ctime) > old snap ctime
  while (!is_error && !dir_queue.empty()) {
    std::string dir_name = dir_queue.front();
    dir_queue.pop();

    if (ceph_opendir(m_src_mnt, ("."s + dir_name).c_str(), &new_dirp) < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": opendir failed for " << ("." + dir_name) << std::endl;
      is_error = true;
      break;
    }
    ceph_assert(std::string(ceph_readdir(m_src_mnt, new_dirp)->d_name) == ".");
    ceph_assert(std::string(ceph_readdir(m_src_mnt, new_dirp)->d_name) == "..");

    while (ceph_readdirplus_r(m_src_mnt, new_dirp, &de, &new_stx, CEPH_STATX_BASIC_STATS, AT_SYMLINK_NOFOLLOW, NULL) > 0) {
      if (!is_first_sync) {
        std::string old_dentry = m_src_dir      + "/"s +
                                 m_src_snap_dir + "/"s +
                                 m_old_snap     +
                                 dir_name       +
                                 "/"s + de.d_name;

        int rv = 0;
        if ((rv = ceph_statx(m_src_mnt, old_dentry.c_str(), &old_stx, CEPH_STATX_BASIC_STATS, AT_SYMLINK_NOFOLLOW)) == 0) {
          // found entry in old snap
          std::clog << __func__ << ":" << __LINE__ << ": processing old entry " << old_dentry << std::endl;
          if (handle_old_entry(dir_name, old_dentry, de, old_stx, new_stx, old_rctime, dir_queue) < 0) {
            is_error = true;
            break;
          }
        } else if (rv == -ENOENT) {
          goto new_entry;
        } else {
          std::clog << __func__ << ":" << __LINE__ << ": error while getting statx for old entry " << old_dentry << std::endl;
          is_error = true;
          break;
        }
      } else {
new_entry:
        std::string dentry = dir_name + "/"s + de.d_name;
        std::string dst_dentry_path = m_src_dir + dentry;

        std::clog << __func__ << ":" << __LINE__ << ": processing new entry " << dst_dentry_path << std::endl;
        if (handle_new_entry(dst_dentry_path, dentry, de, new_stx, dir_queue) < 0) {
          is_error = true;
          break;
        }
      }
    }
    if (ceph_closedir(m_src_mnt, new_dirp) < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": closedir failed for " << ("." + dir_name) << std::endl;
    }
    new_dirp = NULL;
  }
  if (!is_error) {
    int rv = 0;
    std::string dst_snap = m_src_dir + "/"s + m_dst_snap_dir + "/"s + m_new_snap;
    if ((rv = ceph_mkdir(m_dst_mnt, dst_snap.c_str(), 0755)) < 0 and rv != -EEXIST) {
      std::clog << __func__ << ":" << __LINE__ << ": failed to create dst snapshot dir " << dst_snap << std::endl;
      my_errno = -rv;
      return -1;
    }
    return 0;
  }
  return -1;
}

struct timespec SnapReplicator::get_rctime(const std::string& snap_name, const std::string& dir)
{
  struct timespec ts{0, 0};

  if (snap_name == ""s)
    return ts;

  char buf[64] = {0,};
  if (ceph_lgetxattr(m_src_mnt, dir.c_str(), "ceph.dir.rctime", buf, sizeof(buf)) < 0) {
    std::clog << __func__ << ":" << __LINE__ << ": lgetxattr failed for: " << dir << "\n";
    return ts;
  }

  const char *dot = strchr(buf, '.');
  ceph_assert(dot);
  std::string sec = std::string(buf, dot - buf);
  std::string nsec = std::string(dot + 1);
  ts.tv_sec = std::stol(sec);
  ts.tv_nsec = std::stol(nsec);
  return ts;
}

bool SnapReplicator::is_system_dir(const char *dir) const
{
  if ((dir[0] == '.' && dir[1] == '\0') ||
      (dir[0] == '.' && dir[1] == '.' && dir[2] == '\0'))
    return true;

  return false;
}

int SnapReplicator::copy_remaining(int read_fd, int write_fd, off_t read, int len)
{
  bool is_error = false;
  int rv = 0;

  while (read < len) {
    if ((rv = ceph_read(m_src_mnt, read_fd, m_readbuf_new, TXN_BLOCK_SIZE, read)) < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": source file read with ceph_read(" << read_fd << ", " << TXN_BLOCK_SIZE << ", " << read << ") failed\n";
      my_errno = -rv;
      is_error = true;
      break;
    }
    int read_bytes = rv;
    if ((rv = ceph_write(m_dst_mnt, write_fd, m_readbuf_new, rv, read)) < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": destination file write with ceph_write(" << write_fd << ", " << read_bytes << ", " << read << ") failed\n";
      my_errno = -rv;
      is_error = true;
      break;
    }
    read += rv;
  }
  if (is_error)
    return -1;
  return 0;
}

int SnapReplicator::copy_blocks(const std::string& en, file_type_t ftype,
                                const struct ceph_statx& old_stx,
                                const struct ceph_statx& new_stx)
{
  bool is_error = false;

  if (ftype == file_type_t::OLD_FILE) {
    off_t old_size = old_stx.stx_size;

    if (old_size == 0) {
      std::clog << __func__ << ":" << __LINE__ << ": old file size is zero; copying all new file blocks\n";
      goto copy_all_blocks;
    }

    std::string old_root = m_src_dir + "/"s + m_src_snap_dir + "/"s + m_old_snap;

    int rv = ceph_open(m_src_mnt, (old_root + en).c_str(), O_RDONLY, 0);

    if (rv < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": ceph_open(" << (old_root + en) << ") failed\n";
      my_errno = -rv;
      return -1;
    }

    int read_fd_old = rv;
    std::string new_root = m_src_dir + "/"s + m_src_snap_dir + "/"s + m_new_snap;
    rv = ceph_open(m_src_mnt, (new_root + en).c_str(), O_RDONLY, 0);
    if (rv < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": ceph_open(" << (new_root + en) << ") failed\n";
      my_errno = -rv;
      return -1;
    }
    int read_fd_new = rv;

    off_t read = 0;
    const off_t new_size = new_stx.stx_size;

    if (new_size == 0) {
      std::clog << __func__ << ":" << __LINE__ << ": new file size is zero; nothing to copy\n";
      return 0;
    }

    std::clog << __func__ << ":" << __LINE__ << ": starting copy of file blocks\n";
    if (new_size == 0) {
      std::clog << __func__ << ":" << __LINE__ << ": new file size is zero\n";
      return 0;
    }

    rv = ceph_open(m_dst_mnt, (m_src_dir + en).c_str(), O_CREAT|O_WRONLY, new_stx.stx_mode);
    if (rv < 0) {
      my_errno = -rv;
      return -1;
    }

    int write_fd = rv;
    const off_t min_size = std::min(old_size, new_size);

    read = 0;
    while (read < min_size) {
      int rv_old = ceph_read(m_src_mnt, read_fd_old, m_readbuf_old, TXN_BLOCK_SIZE, read);

      if (rv_old < 0) {
        std::clog << __func__ << ":" << __LINE__ << ": failed while reading from old file\n";
        my_errno = -rv_old;
        is_error = true;
        break;
      }

      int rv_new = ceph_read(m_src_mnt, read_fd_new, m_readbuf_new, TXN_BLOCK_SIZE, read);

      if (rv_new < 0) {
        std::clog << __func__ << ":" << __LINE__ << ": failed while reading from new file\n";
        my_errno = -rv_new;
        is_error = true;
        break;
      }

      if (rv_old != rv_new || memcmp(m_readbuf_old, m_readbuf_new, TXN_BLOCK_SIZE) != 0) {
        rv = ceph_write(m_dst_mnt, write_fd, m_readbuf_new, rv_new, read);
        if (rv < 0) {
          std::clog << __func__ << ":" << __LINE__ << ": failed while writing to new file\n";
          my_errno = -rv;
          is_error = true;
          break;
        }
      }

      read += rv_new;
    }

    if (!is_error && min_size < new_size) {
      // copy over the remaining suffix chunk to the destination
      if (copy_remaining(read_fd_new, write_fd, read, new_size) < 0)
        is_error = true;
    }

    if (write_fd)
      ceph_close(m_dst_mnt, write_fd);
    if (read_fd_new)
      ceph_close(m_src_mnt, read_fd_new);
    if (read_fd_old)
      ceph_close(m_src_mnt, read_fd_old);

  } else if (ftype == file_type_t::NEW_FILE) {
copy_all_blocks:
    std::string new_root = m_src_dir + "/"s + m_src_snap_dir + "/"s + m_new_snap;
    int rv = ceph_open(m_src_mnt, (new_root + en).data(), O_RDONLY, 0);
    if (rv < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": opening new source file with ceph_open(" << (new_root + en) << ", O_RDONLY) failed\n";
      my_errno = -rv;
      return -1;
    }
    int read_fd = rv;
    
    rv = ceph_open(m_dst_mnt, (m_src_dir + en).c_str(), O_CREAT|O_WRONLY, new_stx.stx_mode);
    if (rv < 0) {
      std::clog << __func__ << ":" << __LINE__ << ": opening new destination file with ceph_open(" << (en) << ", O_CREAT) failed\n";
      my_errno = -rv;
      ceph_close(m_src_mnt, read_fd);
      return -1;
    }
    int write_fd = rv;
    
    const off_t len = new_stx.stx_size;

    copy_remaining(read_fd, write_fd, 0, len);
    ceph_close(m_dst_mnt, write_fd);
    ceph_close(m_src_mnt, read_fd);
  }
  if (is_error)
    return -1;

  return 0;
}

bool operator ==(const struct timespec& lhs, const struct timespec& rhs)
{
  return ((lhs.tv_sec == rhs.tv_sec) && (lhs.tv_nsec == rhs.tv_nsec));
}

bool operator >(const struct timespec& lhs, const struct timespec& rhs)
{
  if (lhs.tv_sec > rhs.tv_sec)
    return true;

  if (lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec > rhs.tv_nsec)
    return true;

  return false;
}

bool operator >=(const struct timespec& lhs, const struct timespec& rhs)
{
  return ((lhs == rhs) or (lhs > rhs));
}
