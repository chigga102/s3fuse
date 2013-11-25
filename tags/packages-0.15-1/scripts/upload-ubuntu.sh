#!/bin/bash

_UPLOAD=$1
_PPA=$2

if [[ -z "$_UPLOAD" || -z "$_PPA" ]]; then
  echo "usage: $0 <package-source-changes-file> <ppa>"
  exit 1
fi

if [ "${_UPLOAD%%_source.changes}_source.changes" != "$_UPLOAD" ]; then
  echo "upload a file ending in _source.changes!"
  exit 1
fi

dput ppa:$_PPA $_UPLOAD
