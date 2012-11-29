// this file is used to generate config.h, config.cc, and s3fuse.conf
//
// format is:
// CONFIG(type, name, default-value); or CONFIG_REQUIRED(type, name, default-value);
//
// notes:
//   - a semicolon must follow CONFIG() or CONFIG_REQUIRED().
//   - a description can follow.  if it does, it must follow the semicolon and be preceded by " // ".
//   - build-config.sh does not preprocess this file, so there's not much room for variation in formatting

CONFIG(int, default_uid, UID_MAX); // UID for files in S3 that don't have a UID set (defaults to process effective UID)
CONFIG(int, default_gid, GID_MAX); // GID for files in S3 that don't have a GID set (defaults to process effective GID)
CONFIG(int, default_mode, 0755); // mode for files in S3 that don't have a mode set

CONFIG(int, cache_expiry_in_s, 3 * 60); // time in seconds before objects in stats cache expire

CONFIG(std::string, default_content_type, "binary/octet-stream"); // MIME type for newly-created objects
CONFIG(size_t, download_chunk_size, 128 * 1024); // download chunk size, in bytes
CONFIG(size_t, upload_chunk_size, 5 * 1024 * 1024); // upload chunk size, in bytes (5 MB is the S3 minimum)
CONFIG(int, max_transfer_retries, 5); // maximum number of times a chunk transfer will be retried before failing
CONFIG(int, transfer_timeout_in_s, 5 * 60); // transfer timeout in seconds, should be long enough to transfer download_chunk_size/upload_chunk_size

CONFIG(std::string, url_prefix, "https://s3.amazonaws.com"); // S3 URL prefix
CONFIG(bool, verbose_requests, false); // set CURLOPT_VERBOSE (enable verbosity in libcurl)
CONFIG(int, request_timeout_in_s, 10); // request timeout in seconds (for all HTTP requests besides transfers)

CONFIG_REQUIRED(std::string, aws_key, ""); // AWS key
CONFIG_REQUIRED(std::string, aws_secret, ""); // AWS secret
CONFIG_REQUIRED(std::string, bucket_name, ""); // S3 bucket name