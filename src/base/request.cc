/*
 * request.cc
 * -------------------------------------------------------------------------
 * Executes HTTP requests using libcurl.
 * -------------------------------------------------------------------------
 *
 * Copyright (c) 2011, Tarick Bedeir.
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

#include <stdint.h>
#include <string.h>

#include <stdexcept>

#include "config.h"
#include "logger.h"
#include "request.h"
#include "request_hook.h"
#include "ssl_locks.h"
#include "statistics.h"

using std::runtime_error;
using std::string;

using s3::base::request;
using s3::base::statistics;

#define TEST_OK(x) do { if ((x) != CURLE_OK) throw runtime_error("call to " #x " failed."); } while (0)

namespace
{
  class curl_slist_wrapper
  {
  public:
    curl_slist_wrapper()
      : _list(NULL)
    {
    }

    ~curl_slist_wrapper()
    {
      if (_list)
        curl_slist_free_all(_list);
    }

    inline void append(const char *item)
    {
      _list = curl_slist_append(_list, item);
    }

    inline const curl_slist * get() const
    {
      return _list;
    }

  private:
    curl_slist *_list;
  };
}

request::request(const string &tag)
  : _hook(NULL),
    _current_run_time(0.0),
    _total_run_time(0.0),
    _run_count(0),
    _total_bytes_transferred(0),
    _canceled(false),
    _timeout(0),
    _tag(tag)
{
  _curl = curl_easy_init();

  if (!_curl)
    throw runtime_error("curl_easy_init() failed.");

  ssl_locks::init();

  // stuff that's set in the ctor shouldn't be modified elsewhere, since the call to init() won't reset it

  TEST_OK(curl_easy_setopt(_curl, CURLOPT_VERBOSE, config::get_verbose_requests()));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_NOPROGRESS, true));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION, true));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_ERRORBUFFER, _curl_error));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_FILETIME, true));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_NOSIGNAL, true));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_HEADERFUNCTION, &request::process_header));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_HEADERDATA, this));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, &request::process_output));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_WRITEDATA, this));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_READFUNCTION, &request::process_input));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_READDATA, this));
}

request::~request()
{
  curl_easy_cleanup(_curl);

  if (_run_count > 0) {
    statistics::post(
      "request_count",
      _tag,
      "requests: %" PRIu64 ", total_time_s: %.03f, average_per_request_ms: %.02f",
      _run_count, 
      _total_run_time,
      static_cast<double>(_run_count) / _total_run_time * 1.0e3);

    statistics::post(
      "request_throughput",
      _tag,
      "bytes: %" PRIu64 ", total_time_s: %.03f, average_throughput_kbs: %.03f",
      _total_bytes_transferred, 
      _total_run_time,
      static_cast<double>(_total_bytes_transferred) / _total_run_time * 1.0e-3);
  }

  ssl_locks::release();
}

void request::init(http_method method)
{
  if (_canceled)
    throw runtime_error("cannot reuse a canceled request.");

  _curl_error[0] = '\0';
  _url.clear();
  _output_buffer.clear();
  _response_headers.clear();
  _response_code = 0;
  _last_modified = 0;
  _headers.clear();

  TEST_OK(curl_easy_setopt(_curl, CURLOPT_CUSTOMREQUEST, NULL));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_UPLOAD, false));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_NOBODY, false));
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_POST, false));

  if (method == HTTP_DELETE) {
    _method = "DELETE";
    TEST_OK(curl_easy_setopt(_curl, CURLOPT_CUSTOMREQUEST, "DELETE"));
    TEST_OK(curl_easy_setopt(_curl, CURLOPT_NOBODY, true));

  } else if (method == HTTP_GET) {
    _method = "GET";

  } else if (method == HTTP_HEAD) {
    _method = "HEAD";
    TEST_OK(curl_easy_setopt(_curl, CURLOPT_NOBODY, true));

  } else if (method == HTTP_POST) {
    _method = "POST";
    TEST_OK(curl_easy_setopt(_curl, CURLOPT_POST, true));

  } else if (method == HTTP_PUT) {
    _method = "PUT";
    TEST_OK(curl_easy_setopt(_curl, CURLOPT_UPLOAD, true));

  } else
    throw runtime_error("unsupported HTTP method.");

  // set this last because it depends on the value of _method
  set_input_buffer(NULL, 0);
}

size_t request::process_header(char *data, size_t size, size_t items, void *context)
{
  request *req = static_cast<request *>(context);
  char *pos;

  size *= items;

  if (req->_canceled)
    return 0; // abort!

  if (data[size] != '\0')
    return size; // we choose not to handle the case where data isn't null-terminated

  pos = strchr(data, '\n');

  if (pos)
    *pos = '\0';

  // for some reason the ETag header (among others?) contains a carriage return
  pos = strchr(data, '\r'); 

  if (pos)
    *pos = '\0';

  pos = strchr(data, ':');

  if (!pos)
    return size; // no colon means it's not a header we care about

  *pos++ = '\0';

  if (*pos == ' ')
    pos++;

  req->_response_headers[data] = pos;

  return size;
}

size_t request::process_output(char *data, size_t size, size_t items, void *context)
{
  request *req = static_cast<request *>(context);
  size_t old_size;

  // why even bother with "items"?
  size *= items;

  if (req->_canceled)
    return 0; // abort!

  old_size = req->_output_buffer.size();
  req->_output_buffer.resize(old_size + size);
  memcpy(&req->_output_buffer[old_size], data, size);

  return size;
}

size_t request::process_input(char *data, size_t size, size_t items, void *context)
{
  size_t remaining;
  request *req = static_cast<request *>(context);

  size *= items;

  if (req->_canceled)
    return 0; // abort!

  remaining = (size > req->_input_size) ? req->_input_size : size;

  memcpy(data, req->_input_buffer, remaining);
  
  req->_input_buffer += remaining;
  req->_input_size -= remaining;

  return remaining;
}

void request::set_url(const string &url, const string &query_string)
{
  string curl_url = _hook ? _hook->adjust_url(url) : url;

  if (!query_string.empty()) {
    curl_url += ((curl_url.find('?') == string::npos) ? "?" : "&");
    curl_url += query_string;
  }

  _url = url;
  TEST_OK(curl_easy_setopt(_curl, CURLOPT_URL, curl_url.c_str()));
}

void request::set_input_buffer(const char *buffer, size_t size)
{
  _input_buffer = buffer;
  _input_size = size;

  if (_method == "PUT")
    TEST_OK(curl_easy_setopt(_curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(size)));
  else if (_method == "POST")
    TEST_OK(curl_easy_setopt(_curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(size)));
  else if (size)
    throw runtime_error("can't set input data for non-POST/non-PUT request.");
}

bool request::check_timeout()
{
  if (_timeout && time(NULL) > _timeout) {
    S3_LOG(LOG_WARNING, "request::check_timeout", "timed out on url [%s].\n", _url.c_str());

    _canceled = true;
    return true;
  }

  return false;
}

void request::run(int timeout_in_s)
{
  int r = CURLE_OK;
  double elapsed_time;
  uint64_t request_size = 0;

  // sanity
  if (_url.empty())
    throw runtime_error("call set_url() first!");

  if (_method.empty())
    throw runtime_error("call set_method() first!");

  if (_canceled)
    throw runtime_error("cannot reuse a canceled request.");

  for (int iter = 0; iter < config::get_max_transfer_retries(); iter++) {
    curl_slist_wrapper headers;
   
    _output_buffer.clear();
    _response_headers.clear();

    if (_hook)
      _hook->pre_run(this, iter);

    for (header_map::const_iterator itor = _headers.begin(); itor != _headers.end(); ++itor) {
      string header = itor->first + ": " + itor->second;

      headers.append(header.c_str());
      request_size += header.size();
    }

    TEST_OK(curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, headers.get()));

    // we need _input_size before calling curl_easy_perform(), because the input-
    // reading callbacks will wind _input_size down to zero as the input is read
    request_size += _input_size;

    _timeout = time(NULL) + ((timeout_in_s == DEFAULT_REQUEST_TIMEOUT) ? config::get_request_timeout_in_s() : timeout_in_s);
    r = curl_easy_perform(_curl);
    _timeout = 0; // reset this here so that subsequent calls to check_timeout() don't fail

    if (_canceled)
      throw runtime_error("request timed out.");

    if (
      r == CURLE_COULDNT_RESOLVE_PROXY || 
      r == CURLE_COULDNT_RESOLVE_HOST || 
      r == CURLE_COULDNT_CONNECT || 
      r == CURLE_PARTIAL_FILE || 
      r == CURLE_UPLOAD_FAILED || 
      r == CURLE_OPERATION_TIMEDOUT || 
      r == CURLE_SSL_CONNECT_ERROR || 
      r == CURLE_GOT_NOTHING || 
      r == CURLE_SEND_ERROR || 
      r == CURLE_RECV_ERROR || 
      r == CURLE_BAD_CONTENT_ENCODING)
    {
      S3_LOG(LOG_WARNING, "request::run", "got error [%s]. retrying.\n", _curl_error);
      continue;
    }

    if (r == CURLE_OK) {
      TEST_OK(curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &_response_code));
      TEST_OK(curl_easy_getinfo(_curl, CURLINFO_TOTAL_TIME, &elapsed_time));
      TEST_OK(curl_easy_getinfo(_curl, CURLINFO_FILETIME, &_last_modified));

      if (_hook && _hook->should_retry(this, iter))
        continue;
    }

    // break on CURLE_OK or some other error where we don't want to try the request again
    break;
  }

  if (r != CURLE_OK)
    throw runtime_error(_curl_error);

  // don't save the time for the first request since it's likely to be disproportionately large
  if (_run_count > 0) {
    _total_run_time += elapsed_time;
    _total_bytes_transferred += request_size + _output_buffer.size();
  }

  // but save it in _current_run_time since it's compared to overall function time (i.e., it's relative)
  _current_run_time += elapsed_time;

  _run_count++;

  if (_response_code >= HTTP_SC_MULTIPLE_CHOICES && _response_code != HTTP_SC_NOT_FOUND)
    S3_LOG(
      LOG_WARNING, 
      "request::run", 
      "request for [%s] failed with code %i and response: %s\n", 
      _url.c_str(), 
      _response_code, 
      &_output_buffer[0]);
}
