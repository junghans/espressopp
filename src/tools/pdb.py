import espresso
from math import sqrt

def pdbwrite(filename, system):
  file = open(filename,'w')
  s = "REMARK generated by ESPResSo++\n"
  file.write(s)
  maxParticleID = int(espresso.analysis.MaxPID(system).compute())
  count = 1
  pid   = 0
  while pid <= maxParticleID:
    particle = system.storage.getParticle(pid)
    if particle.pos:
        xpos   = particle.pos[0]
        ypos   = particle.pos[1]
        zpos   = particle.pos[2]
        type   = particle.type
        st = "ATOM %6d  FE  UNX F%4d    %8.3f%8.3f%8.3f  0.00  0.00      T%03d\n"%(count, pid % 10000, xpos, ypos, zpos, type)
        file.write(st)
        count += 1
        pid   += 1
    else:
        pid   += 1
  
  file.write('END\n')
  file.close()

def psfwrite(filename, system, maxdist=None):
  file = open(filename,'w')
  maxParticleID = int(espresso.analysis.MaxPID(system).compute())
  nParticles    = int(espresso.analysis.NPart(system).compute())
  
  file.write("PSF\n")
  st = "%8d !NATOM\n" % nParticles
  file.write(st)
  
  count     = 1
  pid       = 0
  pid_count_translate = {}
  while pid <= maxParticleID:
    particle = system.storage.getParticle(pid)
    if particle.pos:
        xpos   = particle.pos[0]
        ypos   = particle.pos[1]
        zpos   = particle.pos[2]
        type   = particle.type
        st = "%8d T%03d %4d UNX  FE   FE                    \n" % (count, type, pid)
        file.write(st)
        pid_count_translate[pid] = count
        count += 1
        pid   += 1
    else:
        pid   += 1
   
  bond = []
  nInteractions = system.getNumberOfInteractions()
  for i in range(nInteractions):
      if system.getInteraction(i).isBonded():
#          try:
              FixedPairList = system.getInteraction(i).getFixedPairList().getBonds()
              j = 0
              while j < len(FixedPairList):
                  fplb = FixedPairList[j]
                  k = 0
                  while k < len(fplb):
                    if maxdist != None:
                      pid1 = fplb[k][0]
                      pid2 = fplb[k][1]
                      p1 = system.storage.getParticle(pid1)
                      p2 = system.storage.getParticle(pid2)
                      x1 = p1.pos[0]
                      y1 = p1.pos[1]
                      z1 = p1.pos[2]
                      x2 = p2.pos[0]
                      y2 = p2.pos[1]
                      z2 = p2.pos[2]
                      xx = (x1-x2) * (x1-x2)
                      yy = (y1-y2) * (y1-y2)
                      zz = (z1-z2) * (z1-z2)
                      d = sqrt( xx + yy + zz )
                      if (d <= maxdist):
                        bond.append(fplb[k])
                    else:
                      bond.append(fplb[k])
                    k += 1                        
                  j += 1
#          except:
#              pass
              
  bond.sort()
  
  file.write("%8d !NBOND\n" % (len(bond)))
  i = 0
  while i < len(bond):
    file.write("%8d%8d" % (bond[i][0], pid_count_translate[bond[i][1]]) )
    if ( ((i+1) % 4) == 0 and (i != 0) ) or i == len(bond)-1 :
      file.write("\n")
    i += 1

  file.close()