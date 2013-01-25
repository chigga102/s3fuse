/*
 * fs/encryption.h
 * -------------------------------------------------------------------------
 * Filesystem encryption methods.
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

#ifndef S3_FS_ENCRYPTION_H
#define S3_FS_ENCRYPTION_H

#include <string>
#include <boost/smart_ptr.hpp>

namespace s3
{
  namespace crypto
  {
    class buffer;
  }

  namespace fs
  {
    class encryption
    {
    public:
      static void init();

      // TODO: split all this stuff off into class volume_key?
      static boost::shared_ptr<crypto::buffer> get_volume_key();

      static boost::shared_ptr<crypto::buffer> derive_key_from_password(const std::string &password);

      static bool is_volume_key_present_in_bucket();

      static void write_new_volume_key_to_bucket(const boost::shared_ptr<crypto::buffer> &key);

      static void reencrypt_volume_key_in_bucket(
        const boost::shared_ptr<crypto::buffer> &old_key, 
        const boost::shared_ptr<crypto::buffer> &new_key);

      static void delete_volume_key_from_bucket();
    };
  }
}

#endif
