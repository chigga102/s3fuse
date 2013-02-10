#!/bin/bash

_CONF=$1

if [ ! -f "$_CONF" ]; then
  echo "can't find config file [$_CONF]."
  exit 1
fi

echo '.\" man page for s3fuse'
echo '.\" generated by s3fuse.conf.5.in'

echo '.TH S3FUSE.CONF 5 2013-01-26 "s3fuse 0.13" "s3fuse Configuration File"'
echo ''
echo '.SH NAME'
echo '\fBs3fuse.conf\fR - Configuration file for s3fuse'
echo ''
echo '.SH FILES'
echo '\fBs3fuse\fR(1) loads a configuration file on initialization. It tries'
echo 'the following paths, in order:'
echo '.IP \[bu] 2'
echo '.B "~/.s3fuse/s3fuse.conf"'
echo ''
echo '.IP \[bu]'
echo ".B \"$SYSCONFDIR/s3fuse.conf\""
echo ''
echo '.SH SYNTAX'
echo 'The configuration file contains a list of parameters, one per line, in'
echo 'the format \fIparameter_name\fR=\fIparameter_value\fR. Lines beginning'
echo 'with a # are ignored.'

grep '^\(CONFIG\|CONFIG_REQUIRED\|CONFIG_SECTION\)(.*);$' $_CONF | \
  sed -e 's/(/%%%%/' -e 's/);$//' -e 's/, /%%%%/g' -e 's/"//g' | \
  awk -F '%%%%' '{ \
    if ($1 == "CONFIG_SECTION") { \
      print "\n.SH " toupper($2) \
    } else if ($1 == "CONFIG_REQUIRED" || $1 == "CONFIG") { \
      print ".TP\n.B " $3 "\n" $5 \
    } \
  }'

echo '.SH SEE ALSO'
echo '\fBs3fuse\fR(1), \fBs3fuse_gs_get_token\fR(1), \fBs3fuse_vol_key\fR(1)'
