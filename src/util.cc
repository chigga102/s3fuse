/*
 * util.cc
 * -------------------------------------------------------------------------
 * MD5 digests, various encodings.
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

#include <string.h>
#include <unistd.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <stdexcept>

#include "util.h"

using namespace s3;
using namespace std;

namespace
{
  inline int int_from_hex(char c)
  {
    if (c >= '0' && c <= '9')
      return c - '0';

    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;

    throw runtime_error("invalid character in hex string.");
  }

  inline char hex_from_int(int i)
  {
    const char *hex = "0123456789abcdef";

    return hex[i];
  }
}

string util::base64_encode(const uint8_t *input, size_t size)
{
  BIO *bio_b64 = BIO_new(BIO_f_base64());
  BIO *bio_mem = BIO_new(BIO_s_mem());
  BUF_MEM *mem;
  string ret;

  bio_b64 = BIO_push(bio_b64, bio_mem);
  BIO_write(bio_b64, input, size);
  void(BIO_flush(bio_b64));
  BIO_get_mem_ptr(bio_b64, &mem);

  ret.resize(mem->length - 1);
  memcpy(&ret[0], mem->data, mem->length - 1);

  BIO_free_all(bio_b64);
  return ret;
}

string util::sign(const string &key, const string &data)
{
  unsigned int hmac_md5_len;
  uint8_t hmac_md5[EVP_MAX_MD_SIZE];

  HMAC(
    EVP_sha1(), 
    reinterpret_cast<const void *>(key.data()), 
    key.size(), 
    reinterpret_cast<const uint8_t *>(data.data()), 
    data.size(), 
    hmac_md5, 
    &hmac_md5_len);

  return base64_encode(hmac_md5, hmac_md5_len);
}

string util::compute_md5(int fd, md5_output_type type, ssize_t size, off_t offset)
{
  const ssize_t buf_len = 8 * 1024;

  EVP_MD_CTX md5_ctx;
  char buf[buf_len];
  uint8_t md5_buf[EVP_MAX_MD_SIZE];
  unsigned int md5_len = 0;

  if (size == 0 && offset == 0)
    size = -1;

  EVP_DigestInit(&md5_ctx, EVP_md5());

  while (true) {
    ssize_t read_bytes, read_count;

    read_bytes = (size < 0 || size > buf_len) ? buf_len : size;
    read_count = pread(fd, buf, read_bytes, offset);

    if (read_count == -1)
      throw runtime_error("error while computing md5, in pread().");

    EVP_DigestUpdate(&md5_ctx, buf, read_count);

    offset += read_count;
    size -= read_count;

    if (read_count < buf_len)
      break;
  }

  EVP_DigestFinal(&md5_ctx, md5_buf, &md5_len);

  if (type == MOT_BASE64)
    return base64_encode(md5_buf, md5_len);
  else if (type == MOT_HEX)
    return "\"" + hex_encode(md5_buf, md5_len) + "\"";
  else
    throw runtime_error("unknown md5 output type.");
}

string util::hex_encode(const uint8_t *input, size_t size)
{
  string ret;

  ret.resize(size * 2);

  for (size_t i = 0; i < size; i++) {
    ret[2 * i + 0] = hex_from_int(static_cast<uint8_t>(input[i]) / 16);
    ret[2 * i + 1] = hex_from_int(static_cast<uint8_t>(input[i]) % 16);
  }

  return ret;
}

void util::hex_decode(const string &input, vector<uint8_t> *out)
{
  if (input.size() % 2)
    throw runtime_error("input size not even.");

  out->resize(input.size() / 2);

  for (size_t i = 0; i < out->size(); i++)
    (*out)[i] = int_from_hex(input[2 * i + 0]) * 16 + int_from_hex(input[2 * i + 1]);
}

string util::url_encode(const string &url)
{
  const char *hex = "0123456789ABCDEF";
  string ret;

  ret.reserve(url.length());

  for (size_t i = 0; i < url.length(); i++) {
    if (url[i] == '/' || url[i] == '.' || url[i] == '-' || url[i] == '*' || url[i] == '_' || isalnum(url[i]))
      ret += url[i];
    else {
      // allow spaces to be encoded as "%20" rather than "+" because Google
      // Storage doesn't decode the same way AWS does

      ret += '%';
      ret += hex[static_cast<uint8_t>(url[i]) / 16];
      ret += hex[static_cast<uint8_t>(url[i]) % 16];
    }
  }

  return ret;
}

double util::get_current_time()
{
  timeval t;

  gettimeofday(&t, NULL);

  return double(t.tv_sec) + double(t.tv_usec) / 1.0e6;
}

bool util::is_valid_md5(const string &md5)
{
  // yes, it's a rather rudimentary check (32 for the MD5, 2 for the quotes)
  return (md5.size() == 34 && md5[0] == '"' && md5[md5.size() - 1] == '"');
}

void util::open_private_file(const string &file, ofstream *f, ios_base::openmode mode)
{
  f->open(file.c_str(), ios::out | mode);

  if (!f->good())
    throw runtime_error("unable to open/create private file.");

  if (chmod(file.c_str(), S_IRUSR | S_IWUSR))
    throw runtime_error("failed to set permissions on private file.");
}

void util::open_private_file(const string &file, ifstream *f, ios_base::openmode mode)
{
  struct stat s;

  f->open(file.c_str(), ios::in | mode);

  if (!f->good())
    throw runtime_error("unable to open private file.");

  if (stat(file.c_str(), &s))
    throw runtime_error("unable to stat private file.");

  if ((s.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) != (S_IRUSR | S_IWUSR))
    throw runtime_error("private file must be readable/writeable only by owner.");
}
