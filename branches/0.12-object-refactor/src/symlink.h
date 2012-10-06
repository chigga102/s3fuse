#ifndef S3_SYMLINK_H
#define S3_SYMLINK_H

#include "object.h"
#include "thread_pool.h"

namespace s3
{
  class symlink : public object
  {
  public:
    typedef boost::shared_ptr<symlink> ptr;

    symlink(const std::string &path);
    virtual ~symlink();

    inline ptr shared_from_this()
    {
      return boost::static_pointer_cast<symlink>(object::shared_from_this());
    }

    inline int read(std::string *target)
    {
      return thread_pool::call(thread_pool::PR_FG, bind(&symlink::read, shared_from_this(), _1, target));
    }

  private:
    int read(const boost::shared_ptr<request> &req, std::string *target);
  };
}

#endif
