# -*- coding: utf-8 -*-
import sys
p = 'src/sgs_farm.cpp'
s = open(p, encoding='utf-8').read()
NL = chr(10)
BS = chr(92)
bad = '"submit(含挂起)=%.1f argmax=%.1f | multi循环=%.1f' + NL + '",'
good = '"submit(含挂起)=%.1f argmax=%.1f | multi循环=%.1f' + BS + 'n",'
assert bad in s, 'pattern not found'
s = s.replace(bad, good)
open(p, 'w', encoding='utf-8').write(s)
print('fixed')
