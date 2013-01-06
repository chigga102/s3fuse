/*
 * fs/object.cc
 * -------------------------------------------------------------------------
 * Read/write object metadata from/to S3.
 * -------------------------------------------------------------------------
 *
 * Copyright (c) 2012, Tarick Bedeir.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <sys/xattr.h>

#include "base/config.h"
#include "base/logger.h"
#include "base/request.h"
#include "base/xml.h"
#include "fs/cache.h"
#include "fs/glacier.h"
#include "fs/metadata.h"
#include "fs/object.h"
#include "fs/static_xattr.h"
#include "services/service.h"

#ifndef ENOATTR
  #ifndef ENODATA
    #error Need either ENOATTR or ENODATA!
  #endif

  #define ENOATTR ENODATA
#endif

#ifndef __APPLE__
  #define NEED_XATTR_PREFIX
#endif

using boost::mutex;
using std::multimap;
using std::runtime_error;
using std::string;
using std::vector;

using s3::base::config;
using s3::base::header_map;
using s3::base::request;
using s3::base::xml;
using s3::fs::object;
using s3::fs::static_xattr;
using s3::services::service;
using s3::threads::pool;

namespace
{
  const int BLOCK_SIZE = 512;
  const char *COMMIT_ETAG_XPATH = "/s3:CopyObjectResult/s3:ETag";

  #ifdef NEED_XATTR_PREFIX
    const string XATTR_PREFIX = "user.";
    const size_t XATTR_PREFIX_LEN = XATTR_PREFIX.size();
  #else
    const size_t XATTR_PREFIX_LEN = 0;
  #endif

  const int USER_XATTR_FLAGS = 
    s3::fs::xattr::XM_WRITABLE | 
    s3::fs::xattr::XM_SERIALIZABLE | 
    s3::fs::xattr::XM_VISIBLE | 
    s3::fs::xattr::XM_REMOVABLE | 
    s3::fs::xattr::XM_COMMIT_REQUIRED;
}

int object::get_block_size()
{
  return BLOCK_SIZE;
}

string object::build_url(const string &path)
{
  return service::get_bucket_url() + "/" + request::url_encode(path);
}

object::ptr object::create(const string &path, const request::ptr &req)
{
  ptr obj;

  if (!path.empty() && req->get_response_code() != base::HTTP_SC_OK)
    return obj;

  for (type_checker_list::const_iterator itor = type_checker_list::begin(); itor != type_checker_list::end(); ++itor) {
    obj.reset(itor->second(path, req));

    if (obj)
      break;
  }

  if (!obj)
    throw runtime_error("couldn't figure out object type!");

  obj->init(req);

  return obj;
}

int object::copy_by_path(const request::ptr &req, const string &from, const string &to)
{
  req->init(base::HTTP_PUT);
  req->set_url(object::build_url(to));
  req->set_header(service::get_header_prefix() + "copy-source", object::build_url(from));
  req->set_header(service::get_header_prefix() + "metadata-directive", "COPY");

  req->run();

  return (req->get_response_code() == base::HTTP_SC_OK) ? 0 : -EIO;
}

int object::remove_by_url(const request::ptr &req, const string &url)
{
  req->init(base::HTTP_DELETE);
  req->set_url(url);

  req->run();

  return (req->get_response_code() == base::HTTP_SC_NO_CONTENT) ? 0 : -EIO;
}

object::object(const string &path)
  : _path(path),
    _expiry(0)
{
  memset(&_stat, 0, sizeof(_stat));

  _stat.st_nlink = 1; // laziness (see FUSE FAQ re. find)
  _stat.st_blksize = BLOCK_SIZE;
  _stat.st_mode = config::get_default_mode();
  _stat.st_uid = config::get_default_uid();
  _stat.st_gid = config::get_default_gid();
  _stat.st_mtime = time(NULL);

  if (_stat.st_uid == UID_MAX)
    _stat.st_uid = geteuid();

  if (_stat.st_gid == GID_MAX)
    _stat.st_gid = getegid();

  _content_type = config::get_default_content_type();
  _url = build_url(_path);
}

object::~object()
{
}

bool object::is_removable()
{
  return true;
}

void object::copy_stat(struct stat *s)
{
  memcpy(s, &_stat, sizeof(_stat));
}

int object::set_metadata(const string &key, const char *value, size_t size, int flags, bool *needs_commit)
{
  mutex::scoped_lock lock(_mutex);
  string user_key = key.substr(XATTR_PREFIX_LEN);
  xattr_map::iterator itor = _metadata.find(user_key);

  *needs_commit = false;

  #ifdef NEED_XATTR_PREFIX
    if (key.substr(0, XATTR_PREFIX_LEN) != XATTR_PREFIX)
      return -EINVAL;
  #endif

  if (flags & XATTR_CREATE && itor != _metadata.end())
    return -EEXIST;

  if (itor == _metadata.end()) {
    if (flags & XATTR_REPLACE)
      return -ENOATTR;

    itor = _metadata.insert(static_xattr::create(
      user_key, 
      USER_XATTR_FLAGS)).first;
  }

  // since we show read-only keys in get_metadata_keys(), an application might reasonably 
  // assume that it can set them too.  since we don't want it failing for no good reason, 
  // we'll fail silently.

  if (!itor->second->is_writable())
    return 0;

  *needs_commit = itor->second->is_commit_required();

  return itor->second->set_value(value, size);
}

void object::get_metadata_keys(vector<string> *keys)
{
  mutex::scoped_lock lock(_mutex);

  for (xattr_map::const_iterator itor = _metadata.begin(); itor != _metadata.end(); ++itor) {
    if (itor->second->is_visible()) {
      #ifdef NEED_XATTR_PREFIX
        keys->push_back(XATTR_PREFIX + itor->first);
      #else
        keys->push_back(itor->first);
      #endif
    }
  }
}

int object::get_metadata(const string &key, char *buffer, size_t max_size)
{
  mutex::scoped_lock lock(_mutex);
  xattr::ptr value;
  string user_key = key.substr(XATTR_PREFIX_LEN);
  xattr_map::const_iterator itor;

  #ifdef NEED_XATTR_PREFIX
    if (key.substr(0, XATTR_PREFIX_LEN) != XATTR_PREFIX)
      return -ENOATTR;
  #endif

  itor = _metadata.find(user_key);

  if (itor == _metadata.end())
    return -ENOATTR;

  return itor->second->get_value(buffer, max_size);
}

int object::remove_metadata(const string &key)
{
  mutex::scoped_lock lock(_mutex);
  xattr_map::iterator itor = _metadata.find(key.substr(XATTR_PREFIX_LEN));

  if (itor == _metadata.end() || !itor->second->is_removable())
    return -ENOATTR;

  _metadata.erase(itor);
  return 0;
}

void object::set_mode(mode_t mode)
{
  mode = mode & ~S_IFMT;

  if (mode == 0)
    mode = config::get_default_mode();

  _stat.st_mode = (_stat.st_mode & S_IFMT) | mode;
}

void object::init(const request::ptr &req)
{
  // this doesn't need to lock the metadata mutex because the object won't be in the cache (and thus
  // isn't shareable) until the request has finished processing

  const string &meta_prefix = service::get_header_meta_prefix();

  _content_type = req->get_response_header("Content-Type");
  _etag = req->get_response_header("ETag");

  _intact = (_etag == req->get_response_header(meta_prefix + metadata::LAST_UPDATE_ETAG));

  _stat.st_size = strtol(req->get_response_header("Content-Length").c_str(), NULL, 0);
  _stat.st_mode = (_stat.st_mode & S_IFMT) | (strtol(req->get_response_header(meta_prefix + metadata::MODE).c_str(), NULL, 0) & ~S_IFMT);
  _stat.st_uid = strtol(req->get_response_header(meta_prefix + metadata::UID).c_str(), NULL, 0);
  _stat.st_gid = strtol(req->get_response_header(meta_prefix + metadata::GID).c_str(), NULL, 0);
  _stat.st_mtime = strtol(req->get_response_header(meta_prefix + metadata::LAST_MODIFIED_TIME).c_str(), NULL, 0);

  for (header_map::const_iterator itor = req->get_response_headers().begin(); itor != req->get_response_headers().end(); ++itor) {
    const string &key = itor->first;
    const string &value = itor->second;

    if (
      strncmp(key.c_str(), meta_prefix.c_str(), meta_prefix.size()) == 0 &&
      strncmp(key.c_str() + meta_prefix.size(), metadata::RESERVED_PREFIX, strlen(metadata::RESERVED_PREFIX)) != 0
    ) {
      _metadata.replace(static_xattr::from_header(
        key.substr(meta_prefix.size()), 
        value, 
        USER_XATTR_FLAGS));
    }
  }

  _metadata.replace(static_xattr::from_string("s3fuse_content_type", _content_type, xattr::XM_VISIBLE));
  _metadata.replace(static_xattr::from_string("s3fuse_etag", _etag, xattr::XM_VISIBLE));

  // this workaround is for cases when the file was updated by someone else and the mtime header wasn't set
  if (!is_intact() && req->get_last_modified() > _stat.st_mtime)
    _stat.st_mtime = req->get_last_modified();

  _stat.st_blocks = (_stat.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

  // setting _expiry > 0 makes this object valid
  _expiry = time(NULL) + config::get_cache_expiry_in_s();

  if (config::get_allow_glacier_restores()) {
    _glacier = glacier::create(this, req);

    _metadata.replace(_glacier->get_storage_class_xattr());
    _metadata.replace(_glacier->get_restore_ongoing_xattr());
    _metadata.replace(_glacier->get_restore_expiry_xattr());
    _metadata.replace(_glacier->get_request_restore_xattr());
  }
}

void object::set_request_headers(const request::ptr &req)
{
  mutex::scoped_lock lock(_mutex);
  const string &meta_prefix = service::get_header_meta_prefix();
  char buf[16];

  // do this first so that we overwrite any keys we care about (i.e., those that start with "META_PREFIX-META_PREFIX_RESERVED-")
  for (xattr_map::const_iterator itor = _metadata.begin(); itor != _metadata.end(); ++itor) {
    string key, value;

    if (!itor->second->is_serializable())
      continue;

    itor->second->to_header(&key, &value);
    req->set_header(meta_prefix + key, value);
  }

  snprintf(buf, 16, "%#o", _stat.st_mode & ~S_IFMT);
  req->set_header(meta_prefix + metadata::MODE, buf);

  snprintf(buf, 16, "%i", _stat.st_uid);
  req->set_header(meta_prefix + metadata::UID, buf);

  snprintf(buf, 16, "%i", _stat.st_gid);
  req->set_header(meta_prefix + metadata::GID, buf);

  snprintf(buf, 16, "%li", _stat.st_mtime);
  req->set_header(meta_prefix + metadata::LAST_MODIFIED_TIME, buf);

  req->set_header(meta_prefix + metadata::LAST_UPDATE_ETAG, _etag);
  req->set_header("Content-Type", _content_type);
}

void object::set_request_body(const request::ptr &req)
{
}

int object::commit(const request::ptr &req)
{
  // since the etag can change as a result of the copy, we need to run this
  // at most twice so that the second pass will have an updated last-update
  // etag

  for (int i = 0; i < 2; i++) {
    xml::document doc;
    string response, new_etag;
    int r;

    req->init(base::HTTP_PUT);
    req->set_url(_url);

    // if the object already exists (i.e., if we have an etag) then just update
    // the metadata.

    if (!_etag.empty()) {
      req->set_header(service::get_header_prefix() + "copy-source", _url);
      req->set_header(service::get_header_prefix() + "copy-source-if-match", _etag);
      req->set_header(service::get_header_prefix() + "metadata-directive", "REPLACE");
    }

    set_request_headers(req);
    set_request_body(req);

    req->run();

    if (req->get_response_code() != base::HTTP_SC_OK) {
      S3_LOG(LOG_WARNING, "object::commit", "failed to commit object metadata for [%s].\n", _url.c_str());
      return -EIO;
    }

    response = req->get_output_string();

    // an empty response means the etag hasn't changed
    if (response.empty())
      return 0;

    // if we started out without an etag, then ignore anything we get here
    if (_etag.empty())
      return 0;

    doc = xml::parse(response);

    if (!doc) {
      S3_LOG(LOG_WARNING, "object::commit", "failed to parse response.\n");
      return -EIO;
    }

    if ((r = xml::find(doc, COMMIT_ETAG_XPATH, &new_etag)))
      return r;

    if (new_etag.empty()) {
      S3_LOG(LOG_WARNING, "object::commit", "no etag after commit.\n");
      return -EIO;
    }

    // if the etag hasn't changed, don't re-commit
    if (new_etag == _etag)
      return 0;

    S3_LOG(LOG_WARNING, "object::commit", "commit resulted in new etag. recommitting.\n");

    _etag = new_etag;
  }

  return 0;
}

int object::remove(const request::ptr &req)
{
  if (!is_removable())
    return -EBUSY;

  cache::remove(get_path());

  return object::remove_by_url(req, _url);
}

int object::rename(const request::ptr &req, const string &to)
{
  int r;

  if (!is_removable())
    return -EBUSY;
 
  r = object::copy_by_path(req, _path, to);

  if (r)
    return r;

  cache::remove(get_path());

  return remove(req);
}