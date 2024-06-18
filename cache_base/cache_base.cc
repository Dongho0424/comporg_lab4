// ECE 430.322: Computer Organization
// Lab 4: Memory System Simulation

/**
 *
 * This is the base cache structure that maintains and updates the tag store
 * depending on a cache hit or a cache miss. Note that the implementation here
 * will be used throughout Lab 4. 
 */

#include "cache_base.h"

#include <cmath>
#include <string>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iomanip>

///////////////////////////////////////////////////////////////////
// cache_set_c 
///////////////////////////////////////////////////////////////////

/**
 * This allocates an "assoc" number of cache entries per a set
 * @param assoc - number of cache entries in a set
 */
cache_set_c::cache_set_c(int assoc) {
  m_entry = new cache_entry_c[assoc];
  m_assoc = assoc;

  for (int i = 0; i < m_assoc; ++i) {
    cache_entry_c* entry = &m_entry[i];
    m_lru_stack.push_back(entry);
  }
}

// cache_set_c destructor
cache_set_c::~cache_set_c() {
  delete[] m_entry;
}

///////////////////////////////////////////////////////////////////
// cache_base_c 
// 
// <set_list>
// set0 : cache_set0 [cache_entry00, cache_entry01, ...]
// set1 : cache_set1 [cache_entry10, cache_entry11, ...]
// set1 : cache_set2 [cache_entry20, cache_entry21, ...]
//
///////////////////////////////////////////////////////////////////

/**
 * This constructor initializes a cache structure based on the cache parameters.
 * @param name - cache name; use any name you want
 * @param num_sets - number of sets in a cache
 * @param assoc - number of cache entries in a set
 * @param line_size - cache block (line) size in bytes
 *
 * @note Test Note.
 */
cache_base_c::cache_base_c(std::string name, int num_sets, int assoc, int line_size) {
  m_name = name;
  m_num_sets = num_sets;
  m_line_size = line_size;

  m_set_list = new cache_set_c *[m_num_sets];

  for (int ii = 0; ii < m_num_sets; ++ii) {
    m_set_list[ii] = new cache_set_c(assoc);

    // initialize tag/valid/dirty bits
    for (int jj = 0; jj < assoc; ++jj) {
      m_set_list[ii]->m_entry[jj].m_valid = false;
      m_set_list[ii]->m_entry[jj].m_dirty = false;
      m_set_list[ii]->m_entry[jj].m_tag   = 0;
    }
  }

  // initialize stats
  m_num_accesses = 0;
  m_num_hits = 0;
  m_num_misses = 0;
  m_num_writes = 0;
  m_num_writebacks = 0;
}

// cache_base_c destructor
cache_base_c::~cache_base_c() {
  for (int ii = 0; ii < m_num_sets; ++ii) { delete m_set_list[ii]; }
  delete[] m_set_list;
}

/** 
 * This function looks up in the cache for a memory reference.
 * This needs to update all the necessary meta-data (e.g., tag/valid/dirty) 
 * and the cache statistics, depending on a cache hit or a miss.
 * @param address - memory address 
 * @param access_type - read (0), write (1), or instruction fetch (2)
 * @param is_fill - if the access is for a cache fill
 * @param return "true" on a hit; "false" otherwise.
 */
bool cache_base_c::access(addr_t address, int access_type, bool is_fill) {
  ////////////////////////////////////////////////////////////////////
  // \TODO: Write the code to implement this function
  ////////////////////////////////////////////////////////////////////
  
  m_num_accesses++;

  int tag = address / (m_num_sets * m_line_size);
  int set_index = (address / m_line_size) % m_num_sets;

  cache_set_c* set = m_set_list[set_index];
  // Check if there is a cache hit
  bool hit = false;
  int hit_index = -1;

  for (int i = 0; i < set->m_assoc; ++i) {
    if (set->m_entry[i].m_valid && set->m_entry[i].m_tag == tag) {
      hit = true;
      hit_index = i;
      break;
    }
  }

  if (hit) { // Cache hit
    m_num_hits++;
    if (access_type == WRITE) {
      // Write access
      m_num_writes++;
      set->m_entry[hit_index].m_dirty = true;
    }

    // update LRU
    // just hit cache line -> MRU
    set->m_lru_stack.remove(&set->m_entry[hit_index]);
    set->m_lru_stack.push_front(&set->m_entry[hit_index]);
    // cache_entry_c tmp = set->m_entry[hit_index];
    // set->m_lru_stack.remove(&tmp);
    // set->m_lru_stack.push_front(&tmp);  

  } else { // Cache miss
    m_num_misses++;

    // Write access
    if (access_type == WRITE) {
      m_num_writes++;
    }

    // Cache fill when cache miss (read miss fill, write miss fill, IF miss fill)
    if (is_fill) {
      for (int i = 0; i < set->m_assoc; ++i) {
        // Found an empty cache entry 
        if (!set->m_entry[i].m_valid) {
          set->m_entry[i].m_valid = true;
          // A write miss allocates a cacheline in the cache with a dirty flag.
          set->m_entry[i].m_dirty = (access_type == WRITE);
          set->m_entry[i].m_tag = tag;

          // update LRU
          // just filled cache line -> MRU
          set->m_lru_stack.push_front(&set->m_entry[i]);
          if (set->m_lru_stack.size() > set->m_assoc) {
            set->m_lru_stack.pop_back();
          }
          return hit;
        }
      }

      // No empty cache entry found
      // Evict a cache line with LRU policy and fill the new one
      int evict_index = -1;
      if (set->m_lru_stack.size() > 0) {
        evict_index = set->m_lru_stack.back() - set->m_entry;
      } 

      // Evict and Writeback
      if (set->m_entry[evict_index].m_dirty) {
        m_num_writebacks++;
      }

      // Update with new cache entry
      set->m_entry[evict_index].m_valid = true;
      set->m_entry[evict_index].m_dirty = (access_type == WRITE);
      set->m_entry[evict_index].m_tag = tag;    

      // update LRU
      set->m_lru_stack.pop_back();
      set->m_lru_stack.push_front(&set->m_entry[evict_index]);
    }
  }

  return hit;
}

/**
 * Print statistics (DO NOT CHANGE)
 */
void cache_base_c::print_stats() {
  std::cout << "------------------------------" << "\n";
  std::cout << m_name << " Hit Rate: "          << (double)m_num_hits/m_num_accesses*100 << " % \n";
  std::cout << "------------------------------" << "\n";
  std::cout << "number of accesses: "    << m_num_accesses << "\n";
  std::cout << "number of hits: "        << m_num_hits << "\n";
  std::cout << "number of misses: "      << m_num_misses << "\n";
  std::cout << "number of writes: "      << m_num_writes << "\n";
  std::cout << "number of writebacks: "  << m_num_writebacks << "\n";
}


/**
 * Dump tag store (for debugging) 
 * Modify this if it does not dump from the MRU to LRU positions in your implementation.
 */
void cache_base_c::dump_tag_store(bool is_file) {
  auto write = [&](std::ostream &os) { 
    os << "------------------------------" << "\n";
    os << m_name << " Tag Store\n";
    os << "------------------------------" << "\n";

    for (int ii = 0; ii < m_num_sets; ii++) {
      for (int jj = 0; jj < m_set_list[0]->m_assoc; jj++) {
        os << "[" << (int)m_set_list[ii]->m_entry[jj].m_valid << ", ";
        os << (int)m_set_list[ii]->m_entry[jj].m_dirty << ", ";
        os << std::setw(10) << std::hex << m_set_list[ii]->m_entry[jj].m_tag << std::dec << "] ";
      }
      os << "\n";
    }
  };

  if (is_file) {
    std::ofstream ofs(m_name + ".dump");
    write(ofs);
  } else {
    write(std::cout);
  }
}
