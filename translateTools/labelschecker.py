#coding:utf-8 
import sys
readDir = sys.argv[1]
writeDir = sys.argv[2] 
lines_seen = set() 
outfile=open(writeDir,"w") 
f = open(readDir,"r") 
for line in f: 
  if line not in lines_seen: 
    outfile.write(line) 
    lines_seen.add(line) 
outfile.close() 
print("oh yeah")