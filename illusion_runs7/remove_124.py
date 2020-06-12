#!/bin/env python
import argparse

parser = argparse.ArgumentParser(description='Remove line 124 from .m file')
parser.add_argument('--input',type=str,help='file to read from')
parser.add_argument('--output',type=str,help='file to write to')

args = parser.parse_args()

f = open(args.input, 'r')
f2 = open(args.output, 'w')

for i,line in enumerate(f.readlines()):
    if i!=(124-1): #0 index
        f2.write(line)

f.close()
f2.close()

