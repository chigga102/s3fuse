/*
 * fs/fifo.h
 * -------------------------------------------------------------------------
 * Represents a (fake) FIFO object.
 * -------------------------------------------------------------------------
 *
 * Copyright (c) 2013, Tarick Bedeir.
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

#ifndef S3_FS_FIFO_H
#define S3_FS_FIFO_H

#include <string>

#include "fs/file.h"

namespace s3
{
  namespace fs
  {
    class fifo : public file
    {
    public:
      typedef boost::shared_ptr<fifo> ptr;

      fifo(const std::string &path);
      virtual ~fifo();

      inline ptr shared_from_this()
      {
        return boost::static_pointer_cast<fifo>(object::shared_from_this());
      }

    private:
      virtual int open_local_store(int *fd);
    };
  }
}

#endif
