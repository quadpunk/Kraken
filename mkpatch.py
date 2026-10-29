import re,sys
src=open('/home/claude/proj/Source/SynthEngine.h').read()
enum=re.search(r'enum : int\s*\{(.*?)\n\s*COUNT',src,re.S).group(1); enum=re.sub(r'//.*','',enum)
NAMES=[t.strip() for t in enum.split(',') if t.strip()]
# defaults from defaultPatch via a tiny C++ dump
import subprocess,os
open('/tmp/dd.cpp','w').write('#include "/home/claude/proj/Source/SynthEngine.h"\n#include <cstdio>\nint main(){auto p=kraken::defaultPatch();for(float v:p.p)printf("%f ",v);}')
subprocess.run(['g++','-std=c++17','/tmp/dd.cpp','-o','/tmp/dd'],check=True)
D=[float(v) for v in subprocess.run(['/tmp/dd'],capture_output=True,text=True).stdout.split()]
def write(path,note,tune,level,**kw):
    v=list(D)
    for k,val in kw.items(): v[NAMES.index(k)]=val
    open(path,'w').write('%d %.6f %.6f 1 0 0\n%s\n'%(note,tune,level,' '.join('%.6f'%x for x in v)))
