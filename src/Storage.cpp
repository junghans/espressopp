#include <algorithm>

#define LOG4ESPP_LEVEL_DEBUG
#include "log4espp.hpp"

#include "System.hpp"
#include "Storage.hpp"
#include "BC.hpp"
#include "iterator/CellListIterator.hpp"
#include "Real3D.hpp"

using namespace boost;
using namespace espresso;
using namespace espresso::iterator;

LOG4ESPP_LOGGER(Storage::logger, "Storage");

const int STORAGE_COMM_TAG = 0xaa;

const int Storage::dataOfUpdateGhosts = 0;
const int Storage::dataOfExchangeGhosts = DATA_PROPERTIES;

Storage::Storage(shared_ptr< System > _system,
                 const boost::mpi::communicator &_comm)
  : comm(_comm), system(_system)
{
}

Storage::~Storage() {}

longint Storage::getNRealParticles() const {
  longint cnt = 0;
  for (CellList::const_iterator 
	 it = realCells.begin(),
         end = realCells.end();
       it != end; ++it) {
    longint size = (*it)->particles.size();
    if (size) {
      LOG4ESPP_TRACE(logger, "cell " << ((*it) - getFirstCell()) << " size " << size);
    }
    cnt += size;
  }
  return cnt;
}

void Storage::updateLocalParticles(ParticleList &l) {
  for (ParticleList::iterator
	 it  = l.begin(),
	 end = l.end();
       it != end; ++it) {
    localParticles[it->p.id] = &(*it);
  }
}

void Storage::resizeCells(longint nCells) {
  cells.resize(nCells);
  localCells.reserve(nCells);
  for (LocalCellList::iterator
	 it = cells.begin(),
	 end = cells.end(); it != end; ++it) {
    localCells.push_back(&(*it));
  }
}


Particle *Storage::addParticle(longint id, const real p[3])
{
  Cell *cell;

  Particle n;
  n.init();
  n.p.id = id;
  for (int i = 0; i < 3 ; ++i) {
    n.r.p[i] = p[i];
    n.l.i[i] = 0;
  }
  system.lock()->bc->foldPosition(n.r.p, n.l.i);
  cell = mapPositionToCellClipped(n.r.p);

  appendIndexedParticle(cell->particles, n);

  LOG4ESPP_TRACE(logger, "got particle id="
		 << id << " @ " << p[0] << " " << p[1] << " " << p[2] << " ; put it into cell " << cell - getFirstCell());
  LOG4ESPP_TRACE(logger, "folded it to "
		 << n.r.p[0] << " " << n.r.p[1] << " " << n.r.p[2] );
  LOG4ESPP_TRACE(logger, "cell size is now " << cell->particles.size());

  return &cell->particles.back();
}

Particle *Storage::appendUnindexedParticle(ParticleList &l, Particle &part)
{
  l.push_back(part);
  return &l.back();
}

Particle *Storage::appendIndexedParticle(ParticleList &l, Particle &part)
{
  // see whether the array was resized; STL hack
  Particle *begin = &l.front();

  l.push_back(part);
  Particle *p = &l.back();

  if (begin != &l.front())
    updateLocalParticles(l);
  else
    localParticles[p->p.id] = p;
  return p;
}

Particle *Storage::moveUnindexedParticle(ParticleList &dl, ParticleList &sl, int i)
{
  dl.push_back(sl[i]);
  int newSize = sl.size() - 1;
  if (i != newSize) {
    sl[i] = sl.back();
  }
  sl.resize(newSize);
  return &dl.back();
}

Particle *Storage::moveIndexedParticle(ParticleList &dl, ParticleList &sl, int i)
{
  // see whether the arrays were resized; STL hack
  Particle *dbegin = &dl.front();
  Particle *sbegin = &sl.front();

  dl.push_back(sl[i]);
  int newSize = sl.size() - 1;
  if (i != newSize) {
    sl[i] = sl.back();
  }
  sl.resize(newSize);

  Particle *dst = &dl.back();
  Particle *src = &(sl[i]);

  // fix up destination list
  if (dbegin !=  &dl.front()) {
    updateLocalParticles(dl);
  }
  else {
    localParticles[dst->p.id] = dst;
  }
  // fix up resorted source list; due to moving, the last particle
  // might have been moved to the position of the actually moved one
  if (sbegin != &sl.front()) {
    updateLocalParticles(sl);
  }
  else if (i != newSize) {
    localParticles[src->p.id] = src;
  }

  return dst;
}

void Storage::fetchParticles(Storage &old)
{
  LOG4ESPP_DEBUG(logger, "number of received cells = "
		 << old.getRealCells().size());

  for (CellListIterator it(old.getRealCells());
       it.isValid(); ++it) {
    Particle &part = *it;
    Cell *nc = mapPositionToCellClipped(part.r.p);
    appendUnindexedParticle(nc->particles, part);
  }

  // update localParticles
  for(std::vector<Cell *>::iterator
	it = realCells.begin(),
	end = realCells.end();
      it != end; ++it) {
    updateLocalParticles((*it)->particles);
  }
}

void Storage::sendParticles(ParticleList &l, longint node)
{
  LOG4ESPP_DEBUG(logger, "send " << l.size() << " particles to " << node);

  // pack for transport
  mpi::packed_oarchive data(comm);
  int size = l.size();
  data << size;
  for (ParticleList::iterator it = l.begin(), end = l.end(); it != end; ++it)
    data << *it;
  l.clear();

  // ... and send
  comm.send(node, STORAGE_COMM_TAG, data);

  LOG4ESPP_DEBUG(logger, "done");
}

void Storage::recvParticles(ParticleList &l, longint node)
{
  LOG4ESPP_DEBUG(logger, "recv from " << node);

  // receive packed data
  mpi::packed_iarchive data(comm);
  comm.recv(node, STORAGE_COMM_TAG, data);

  // ... and unpack
  int size;
  data >> size;
  int curSize = l.size();
  LOG4ESPP_DEBUG(logger, "got " << size << " particles, have " << curSize);
  if (size > 0) {
    l.resize(curSize + size);

    for (int i = 0; i < size; ++i) {
      data >> l[curSize + i];
    }
  }

  LOG4ESPP_DEBUG(logger, "done");
}

void Storage::invalidateGhosts()
{
  for(CellListIterator it(getGhostCells());
      it.isValid(); ++it) {
    /* remove only ghosts from the hash if the localParticles hash
       actually points to the ghost.  If there are local ghost cells
       to implement pbc, the real particle will be the one accessible
       via localParticles.
    */
    if (localParticles[it->p.id] == &(*it)) {
      localParticles.erase(it->p.id);
    }
  }
}

void Storage::resortParticles()
{
  invalidateGhosts();

  resortRealParticles();

  exchangeGhosts();

  onResortParticles();
}

void Storage::copyRealsToGhosts(Cell &_reals, Cell &_ghosts,
				int elements,
				const double shift[3])
{
  ParticleList &reals  = _reals.particles;
  ParticleList &ghosts = _ghosts.particles;

  LOG4ESPP_DEBUG(logger, "copy data from reals in "
		 << (&_reals - getFirstCell()) << " to ghosts in "
		 << (&_ghosts - getFirstCell()));
  LOG4ESPP_DEBUG(logger, "also copying "
		 << ((elements & DATA_PROPERTIES) ? "properties " : "")
		 << ((elements & DATA_MOMENTUM) ? "momentum " : "")
		 << ((elements & DATA_LOCAL) ? "local " : ""));
  LOG4ESPP_DEBUG(logger, "positions are shifted by "
		 << shift[0] << "," << shift[1] << "," << shift[2]);

  ghosts.resize(reals.size());

  for(ParticleList::iterator src = reals.begin(), end = reals.end(), dst = ghosts.begin();
      src != end; ++src, ++dst) {
    src->r.copyShifted(dst->r, shift);
    if (elements & DATA_PROPERTIES) {
      dst->p = src->p;
    }
    if (elements & DATA_MOMENTUM) {
      dst->m = src->m;
    }
    if (elements & DATA_LOCAL) {
      dst->l = src->l;
    }
    dst->l.ghost = 1;
  }
}

void Storage::addGhostForcesToReals(Cell &_ghosts, Cell &_reals)
{
  LOG4ESPP_DEBUG(logger, "add forces from ghosts in cell "
		 << (&_ghosts - getFirstCell()) << " to reals in cell "
		 << (&_reals - getFirstCell()));

  ParticleList &reals  = _reals.particles;
  ParticleList &ghosts = _ghosts.particles;

  for(ParticleList::iterator dst = reals.begin(), end = reals.end(), src = ghosts.begin();
      dst != end; ++dst, ++src) {

    LOG4ESPP_TRACE(logger, "for particle " << dst->p.id << ": adding force "
		   << src->f.f[0] << " " << src->f.f[1] << " "<< src->f.f[2] << " to "
		   << dst->f.f[0] << " " << dst->f.f[1] << " "<< dst->f.f[2]);

    dst->f += src->f;
  }
}
