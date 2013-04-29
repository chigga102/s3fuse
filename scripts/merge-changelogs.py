#!/usr/bin/python

import re
import sys

def parse_log_entry(entry):
  lines = entry.split('\n')
  release = re.sub(r'^.*\((.*)\).*', r'\1', lines[0]).split('-')
  release = release[0].split('+') + release[1:]

  version = release[0].split('.')
  version = [ (int(v) if v.isdigit() else v) for v in version ]

  release = version + release[1:]

  if not entry.endswith('\n\n'):
    entry += '\n'

  return ( release, entry )

def read_logs(f):
  logs = []
  curr_log = ''

  for line in f.readlines():
    if line[0].isalnum():
      if len(curr_log) > 0:
        logs.append(parse_log_entry(curr_log))
        curr_log = ''

      curr_log = line
    else:
      curr_log += line

  if len(curr_log) > 0:
    logs.append(parse_log_entry(curr_log))

  return logs

if len(sys.argv) < 2:
  print('usage: ' + sys.argv[0] + ' <changelog> [changelogs...]');
  sys.exit(1)

all_logs = []

for arg in sys.argv[1:]:
  f = open(arg, 'r')

  all_logs += read_logs(f)

all_logs = sorted(all_logs, key=lambda log: log[0], reverse=True)

sys.stdout.write(''.join([ l[1] for l in all_logs ]))
