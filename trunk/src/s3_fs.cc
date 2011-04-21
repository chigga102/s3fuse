#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "s3_debug.hh"
#include "s3_fs.hh"
#include "s3_request.hh"
#include "s3_util.hh"

using namespace boost;
using namespace pugi;
using namespace std;

using namespace s3;

// TODO: try/catch everywhere!
// TODO: check error codes after run()

#define ASSERT_NO_TRAILING_SLASH(str) do { if ((str)[(str).size() - 1] == '/') return -EINVAL; } while (0)

namespace
{
  const int BLOCK_SIZE = 512;
  const int STATS_CACHE_EXPIRY_IN_S = 120;

  int g_default_uid = 1000;
  int g_default_gid = 1000;
  int g_default_mode = 0755;

  void get_object_metadata(const request_ptr &req, mode_t *mode, uid_t *uid, gid_t *gid)
  {
    const string &mode_str = req->get_response_header("x-amz-meta-s3fuse-mode");
    const string & uid_str = req->get_response_header("x-amz-meta-s3fuse-uid" );
    const string & gid_str = req->get_response_header("x-amz-meta-s3fuse-gid" );

    *mode = (mode_str.empty() ? g_default_mode : strtol(mode_str.c_str(), NULL, 0));
    * uid = ( uid_str.empty() ? g_default_uid  : strtol( uid_str.c_str(), NULL, 0));
    * gid = ( gid_str.empty() ? g_default_gid  : strtol( gid_str.c_str(), NULL, 0));
  }

  void set_object_metadata(const request_ptr &req, mode_t mode, uid_t uid, gid_t gid)
  {
    char buf[16];

    snprintf(buf, 16, "%#o", mode);
    req->set_header("x-amz-meta-s3fuse-mode", buf);

    snprintf(buf, 16, "%i", uid);
    req->set_header("x-amz-meta-s3fuse-uid", buf);

    snprintf(buf, 16, "%i", gid);
    req->set_header("x-amz-meta-s3fuse-gid", buf);
  }
}

fs::fs(const string &bucket)
  : _bucket(string("/") + util::url_encode(bucket)),
    _prefix_query("/ListBucketResult/CommonPrefixes/Prefix"),
    _key_query("/ListBucketResult/Contents")
{
}

fs::~fs()
{
}

void fs::async_prefill_stats(const string &path, int hints)
{
  struct stat s;

  get_stats(path, NULL, &s, hints);
}

int fs::get_stats(const string &path, string *etag, struct stat *s, int hints)
{
  request_ptr req;
  bool is_directory = (hints == HINT_NONE || hints & HINT_IS_DIR);

  ASSERT_NO_TRAILING_SLASH(path);

  if (_stats_cache.get(path, etag, s))
    return 0;

  req = request::get();
  req->set_method(HTTP_HEAD);

  if (hints == HINT_NONE || hints & HINT_IS_DIR) {
    // see if the path is a directory (trailing /) first
    req->set_url(_bucket + "/" + util::url_encode(path) + "/", "");
    req->run();
  }

  if (hints & HINT_IS_FILE || req->get_response_code() != 200) {
    // it's not a directory
    is_directory = false;
    req->set_url(_bucket + "/" + util::url_encode(path), "");
    req->run();
  }

  if (req->get_response_code() != 200)
    return -ENOENT;

  if (s) {
    const string &length = req->get_response_header("Content-Length");

    memset(s, 0, sizeof(*s));
    get_object_metadata(req, &s->st_mode, &s->st_uid, &s->st_gid);

    // TODO: support symlinks?
    s->st_mode |= (is_directory ? S_IFDIR : S_IFREG);
    s->st_size = strtol(length.c_str(), NULL, 0);
    s->st_nlink = 1; // laziness (see FUSE FAQ re. find)
    s->st_mtime =  req->get_last_modified();

    if (!is_directory)
      s->st_blocks = (s->st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    _stats_cache.update(path, req->get_response_header("ETag"), s);
  }

  if (etag)
    *etag = req->get_response_header("ETag");

  return 0;
}

int fs::change_metadata(const std::string &path, mode_t mode, uid_t uid, gid_t gid)
{
  request_ptr req;
  struct stat s;
  string etag, url;
  int r;

  ASSERT_NO_TRAILING_SLASH(path);

  r = get_stats(path, &etag, &s, HINT_NONE);

  if (r)
    return r;

  if (mode != mode_t(-1)) {
    S3_DEBUG("fs::change_metadata", "changing mode from %#o to %#o.\n", s.st_mode, mode);
    s.st_mode = mode;
  }

  if (uid != uid_t(-1)) {
    S3_DEBUG("fs::change_metadata", "changing user from %i to %i.\n", s.st_uid, uid);
    s.st_uid = uid;
  }

  if (gid != gid_t(-1)) {
    S3_DEBUG("fs::change_metadata", "changing group from %i to %i.\n", s.st_gid, gid);
    s.st_gid = gid;
  }

  // TODO: reuse with create_object
  url = _bucket + "/" + util::url_encode(path);

  if (S_ISDIR(s.st_mode))
    url += "/";

  req = request::get();
  req->set_method(HTTP_PUT);
  req->set_url(url, "");
  req->set_header("Content-Type", "binary/octet-stream");
  req->set_header("x-amz-copy-source", url);
  req->set_header("x-amz-copy-source-if-match", etag);
  req->set_header("x-amz-metadata-directive", "REPLACE");

  set_object_metadata(req, s.st_mode, s.st_uid, s.st_gid);

  req->run();

  if (req->get_response_code() != 200) {
    S3_DEBUG("fs::change_metadata", "response: %s\n", req->get_response_data().c_str());
    return -EIO;
  }

  _stats_cache.remove(path);

  return 0;
}

int fs::read_directory(const std::string &_path, fuse_fill_dir_t filler, void *buf)
{
  request_ptr req;
  size_t path_len;
  string marker = "";
  bool truncated = true;
  string path;

  // TODO: use worker threads to call get_stats in parallel!
  // TODO: openssl locking?

  ASSERT_NO_TRAILING_SLASH(_path);

  if (!_path.empty())
    path = _path + "/";

  path_len = path.size();

  req = request::get();
  req->set_method(HTTP_GET);

  while (truncated) {
    xml_document doc;
    xml_parse_result res;
    xpath_node_set prefixes, keys;

    req->set_url(_bucket, string("delimiter=/&prefix=") + util::url_encode(path) + "&marker=" + marker);
    req->run();

    res = doc.load_buffer(req->get_response_data().data(), req->get_response_data().size());

    truncated = (strcmp(doc.document_element().child_value("IsTruncated"), "true") == 0);
    prefixes = _prefix_query.evaluate_node_set(doc);
    keys = _key_query.evaluate_node_set(doc);

    if (truncated)
      marker = doc.document_element().child_value("NextMarker");

    for (xpath_node_set::const_iterator itor = prefixes.begin(); itor != prefixes.end(); ++itor) {
      const char *full_path_cs = itor->node().child_value();
      const char *relative_path_cs = full_path_cs + path_len;
      string full_path, relative_path;

      // strip trailing slash
      full_path.assign(full_path_cs, strlen(full_path_cs) - 1);
      relative_path.assign(relative_path_cs, strlen(relative_path_cs) - 1);

      S3_DEBUG("fs::read_directory", "found common prefix [%s]\n", relative_path.c_str());

      _async_queue.post(boost::bind(&fs::async_prefill_stats, this, full_path, HINT_IS_DIR));
      filler(buf, relative_path.c_str(), NULL, 0);
    }

    for (xpath_node_set::const_iterator itor = keys.begin(); itor != keys.end(); ++itor) {
      const char *full_path_cs = itor->node().child_value("Key");

      if (strcmp(path.data(), full_path_cs) != 0) {
        string relative_path(full_path_cs + path_len);
        string full_path(full_path_cs);

        S3_DEBUG("fs::read_directory", "found key [%s]\n", relative_path.c_str());

        _async_queue.post(boost::bind(&fs::async_prefill_stats, this, full_path, HINT_IS_FILE));
        filler(buf, relative_path.c_str(), NULL, 0);
      }
    }
  }

  return 0;
}

int fs::create_object(const std::string &path, mode_t mode)
{
  request_ptr req;
  string url;

  ASSERT_NO_TRAILING_SLASH(path);

  url = _bucket + "/" + util::url_encode(path);

  if (get_stats(path, NULL, NULL, HINT_NONE) == 0) {
    S3_DEBUG("fs::create_object", "attempt to overwrite object at path %s.\n", path.c_str());
    return -EEXIST;
  }

  if (S_ISDIR(mode))
    url += "/";

  req = request::get();
  req->set_method(HTTP_PUT);
  req->set_url(url, "");
  req->set_header("Content-Type", "binary/octet-stream");

  if ((mode & ~S_IFMT) == 0) {
    S3_DEBUG("fs::create_object", "no mode specified, using default.\n");
    mode |= g_default_mode;
  }

  // TODO: use parent?
  set_object_metadata(req, mode, g_default_uid, g_default_gid);

  req->run();

  return 0;
}