#!/bin/sh

files=`ls stats*.m`
for f in $files
do
    python remove_124.py --input $f --output clean_$f
done 

