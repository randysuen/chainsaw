
/*
    Copyright (C) 1999-2008 by Mark D. Hill and David A. Wood for the
    Wisconsin Multifacet Project.  Contact: gems@cs.wisc.edu
    http://www.cs.wisc.edu/gems/

    --------------------------------------------------------------------

    This file is part of the Ruby Multiprocessor Memory System Simulator, 
    a component of the Multifacet GEMS (General Execution-driven 
    Multiprocessor Simulator) software toolset originally developed at 
    the University of Wisconsin-Madison.

    Ruby was originally developed primarily by Milo Martin and Daniel
    Sorin with contributions from Ross Dickson, Carl Mauer, and Manoj
    Plakal.

    Substantial further development of Multifacet GEMS at the
    University of Wisconsin was performed by Alaa Alameldeen, Brad
    Beckmann, Jayaram Bobba, Ross Dickson, Dan Gibson, Pacia Harper,
    Derek Hower, Milo Martin, Michael Marty, Carl Mauer, Michelle Moravan,
    Kevin Moore, Andrew Phelps, Manoj Plakal, Daniel Sorin, Haris Volos, 
    Min Xu, and Luke Yen.
    --------------------------------------------------------------------

    If your use of this software contributes to a published paper, we
    request that you (1) cite our summary paper that appears on our
    website (http://www.cs.wisc.edu/gems/) and (2) e-mail a citation
    for your published paper to gems@cs.wisc.edu.

    If you redistribute derivatives of this software, we request that
    you notify us and either (1) ask people to register with us at our
    website (http://www.cs.wisc.edu/gems/) or (2) collect registration
    information and periodically send it to us.

    --------------------------------------------------------------------

    Multifacet GEMS is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General Public
    License as published by the Free Software Foundation.

    Multifacet GEMS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with the Multifacet GEMS; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307, USA

    The GNU General Public License is contained in the file LICENSE.

### END HEADER ###
*/

/*
 * $Id: NodePersistentTable.C 1.3 04/08/16 14:12:33-05:00 beckmann@c2-143.cs.wisc.edu $
 *
 */

#include "NodePersistentTable.h"
#include "Set.h"
#include "Map.h"
#include "Address.h"
#include "AbstractChip.h"
#include "util.h"

// randomize so that handoffs are not locality-aware
// int persistent_randomize[] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
int persistent_randomize[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};


class NodePersistentTableEntry {
public:
  Set m_starving;
  Set m_marked;
  Set m_request_to_write;
};

NodePersistentTable::NodePersistentTable(AbstractChip* chip_ptr, int version)
{
  m_chip_ptr = chip_ptr;
  m_map_ptr = new Map<Address, NodePersistentTableEntry>;
  m_version = version;
}

NodePersistentTable::~NodePersistentTable()
{
  delete m_map_ptr;
  m_map_ptr = NULL;
  m_chip_ptr = NULL;
}  

void NodePersistentTable::persistentRequestLock(const Address& address, NodeID llocker, AccessType type)
{

  // if (locker == m_chip_ptr->getID()  )
  // cout << "Chip " << m_chip_ptr->getID() << ": " << llocker << " requesting lock for " << address << endl;

  NodeID locker = (NodeID) persistent_randomize[llocker];
 
  assert(address == line_address(address));
  if (!m_map_ptr->exist(address)) {
    // Allocate if not present
    NodePersistentTableEntry entry;
    entry.m_starving.add(locker);
    if (type == AccessType_Write) {
      entry.m_request_to_write.add(locker);
    }
    m_map_ptr->add(address, entry);
  } else {
    NodePersistentTableEntry& entry = m_map_ptr->lookup(address);
    assert(!(entry.m_starving.isElement(locker))); // Make sure we're not already in the locked set

    entry.m_starving.add(locker);
    if (type == AccessType_Write) {
      entry.m_request_to_write.add(locker);
    }
    assert(entry.m_marked.isSubset(entry.m_starving));
  }
}

void NodePersistentTable::persistentRequestUnlock(const Address& address, NodeID uunlocker)
{
  // if (unlocker == m_chip_ptr->getID() )
  // cout << "Chip " << m_chip_ptr->getID() << ": " << uunlocker << " requesting unlock for " << address << endl;

  NodeID unlocker = (NodeID) persistent_randomize[uunlocker];

  assert(address == line_address(address));
  assert(m_map_ptr->exist(address));
  NodePersistentTableEntry& entry = m_map_ptr->lookup(address);
  assert(entry.m_starving.isElement(unlocker)); // Make sure we're in the locked set
  assert(entry.m_marked.isSubset(entry.m_starving));
  entry.m_starving.remove(unlocker);
  entry.m_marked.remove(unlocker);
  entry.m_request_to_write.remove(unlocker);
  assert(entry.m_marked.isSubset(entry.m_starving));

  // Deallocate if empty
  if (entry.m_starving.isEmpty()) {
    assert(entry.m_marked.isEmpty());
    m_map_ptr->erase(address);
  }
}

bool NodePersistentTable::okToIssueStarving(const Address& address) const
{
  assert(address == line_address(address));
  if (!m_map_ptr->exist(address)) {
    return true; // No entry present
  } else if (m_map_ptr->lookup(address).m_starving.isElement(m_chip_ptr->getID())) {
    return false; // We can't issue another lockdown until are previous unlock has occurred
  } else {
    return (m_map_ptr->lookup(address).m_marked.isEmpty());
  }
}

NodeID NodePersistentTable::findSmallest(const Address& address) const
{
  assert(address == line_address(address));
  assert(m_map_ptr->exist(address));
  const NodePersistentTableEntry& entry = m_map_ptr->lookup(address);
  // cout << "Node " <<  m_chip_ptr->getID() << " returning " << persistent_randomize[entry.m_starving.smallestElement()] << " for findSmallest(" << address << ")" << endl;
  return (NodeID) persistent_randomize[entry.m_starving.smallestElement()];
}

AccessType NodePersistentTable::typeOfSmallest(const Address& address) const
{
  assert(address == line_address(address));
  assert(m_map_ptr->exist(address));
  const NodePersistentTableEntry& entry = m_map_ptr->lookup(address);
  if (entry.m_request_to_write.isElement(entry.m_starving.smallestElement())) {
    return AccessType_Write;
  } else {
    return AccessType_Read;
  }
}

void NodePersistentTable::markEntries(const Address& address)
{
  assert(address == line_address(address));
  if (m_map_ptr->exist(address)) {
    NodePersistentTableEntry& entry = m_map_ptr->lookup(address);
    assert(entry.m_marked.isEmpty());  // None should be marked
    entry.m_marked = entry.m_starving; // Mark all the nodes currently in the table
  }
}

bool NodePersistentTable::isLocked(const Address& address) const
{
  assert(address == line_address(address));
  // If an entry is present, it must be locked
  return (m_map_ptr->exist(address));
}

int NodePersistentTable::countStarvingForAddress(const Address& address) const
{
  if (m_map_ptr->exist(address)) {
    NodePersistentTableEntry& entry = m_map_ptr->lookup(address);
    return (entry.m_starving.count());
  }
  else {
    return 0;
  }
}

int NodePersistentTable::countReadStarvingForAddress(const Address& address) const
{
  int count = 0;
  if (m_map_ptr->exist(address)) {
    NodePersistentTableEntry& entry = m_map_ptr->lookup(address);
    return (entry.m_starving.count() - entry.m_request_to_write.count());
  }
  else {
    return 0;
  }
}


