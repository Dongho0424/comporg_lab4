// ECE 430.322: Computer Organization
// Lab 4: Memory System Simulation

#include "cache.h"
#include <cstring>
#include <list>
#include <cassert>
#include <iostream>
#include <cmath>

cache_c::cache_c(std::string name, int level, int num_set, int assoc, int line_size, int latency)
    : cache_base_c(name, num_set, assoc, line_size) {

  // instantiate queues
  m_in_queue   = new queue_c();
  m_out_queue  = new queue_c();
  m_fill_queue = new queue_c();
  m_wb_queue   = new queue_c();

  m_in_flight_wb_queue = new queue_c();

  m_id = 0;

  m_prev_i = nullptr;
  m_prev_d = nullptr;
  m_next = nullptr;
  m_memory = nullptr;

  m_latency = latency;
  m_level = level;

  // clock cycle
  m_cycle = 0;
  
  m_num_backinvals = 0;
  m_num_writebacks_backinval = 0;
}

cache_c::~cache_c() {
  delete m_in_queue;
  delete m_out_queue;
  delete m_fill_queue;
  delete m_wb_queue;
  delete m_in_flight_wb_queue;
}

/** 
 * Run a cycle for cache (DO NOT CHANGE)
 */
void cache_c::run_a_cycle() {
  // process the queues in the following order 
  // wb -> fill -> out -> in
  process_wb_queue();
  process_fill_queue();
  process_out_queue();
  process_in_queue();

  ++m_cycle;
}

void cache_c::configure_neighbors(cache_c* prev_i, cache_c* prev_d, cache_c* next, simple_mem_c* memory) {
  m_prev_i = prev_i;
  m_prev_d = prev_d;
  m_next = next;
  m_memory = memory;
}

/**
 *
 * [Cache Fill Flow]
 *
 * This function puts the memory request into fill_queue, so that the cache
 * line is to be filled or written-back.  When we fill or write-back the cache
 * line, it will take effect after the intrinsic cache latency.  Thus, this
 * function adjusts the ready cycle of the request; i.e., a new ready cycle
 * needs to be set for the request.
 *
 */
bool cache_c::fill(mem_req_s* req) {
  req->m_rdy_cycle = m_cycle + m_latency;  // Add the intrinsic cache latency
  m_fill_queue->push(req);        // Put the request into the fill_queue
  return true;
}

/**
 * [Cache Access Flow]
 *
 * This function puts the memory request into in_queue.  When accessing the
 * cache, the outcome (e.g., hit/miss) will be known after the intrinsic cache
 * latency.  Thus, this function adjusts the ready cycle of the request; i.e.,
 * a new ready cycle needs to be set for the request .
 */
bool cache_c::access(mem_req_s* req) { 
  // If req type is Write but is_orig_wr is true, then we need to request as READ type
  // But we cannot explictly change original type.
  // Thus, handling the request of (is_orig_wr == true) is as same as READ type, now on.

  req->m_rdy_cycle = m_cycle + m_latency;  // Add the intrinsic cache latency
  m_in_queue->push(req);          // Put the request into the in_queue
  return true;
}

/** 
 * This function processes the input queue.
 * What this function does are
 * 1. iterates the requests in the queue
 * 2. performs a cache lookup in the "cache base" after the intrinsic access time
 * 3. on a cache hit, forward the request to the prev's fill_queue or the processor depending on the cache level.
 * 4. on a cache miss, put the current requests into out_queue
 */
void cache_c::process_in_queue() {
  auto it = m_in_queue->m_entry.begin();
  if (it == m_in_queue->m_entry.end()) return;
  mem_req_s* req = (*it);

  // before ready cycle
  // yet finishing access() function
  if (req->m_rdy_cycle > m_cycle) {
    return;
  }

  m_in_queue->pop(req);
  
  // Access the cache
  // If Write miss at upper cache (req->m_is_write_miss = true),
  // we have to access this cache as "Read" Request
  if (req->m_type != WRITE && req->m_is_write_miss) {
    std::cout << "write miss error" << std::endl;
  }
  int req_type = req->m_is_write_miss ? READ : req->m_type;
  bool hit = cache_base_c::access(req->m_addr, req_type, false);

  // 1. Read(IF) Hit
  // 1.1 (L1 Cache)   => Done
  // 1.2 (L2) => upper level fill queue
  // 2. Write Hit => Done
  // 3. Read(IF) or Write Miss => out_queue

  // Cache hit
  if (hit) {
    // // 1. Read(IF) Hit
    // if (req_type == REQ_IFETCH || req_type == REQ_DFETCH) {
    //   // 1.1 (L1 Cache) Read Hit => Done
    //   if (m_level == MEM_L1) {
    //     // Done
    //     done_func(req);
    //   }
    //   // 1.2 (L2) => upper level fill queue
    //   else if (m_level == MEM_L2){
    //     // Fill the upper level cache
    //     // Although req is dirty, send as clean to L1
    //     req->m_dirty = false;
    //     m_prev_d->fill(req);
    //   }
    // } 
    // // 2. Write Hit => Done
    // else if (req_type == REQ_DSTORE) {
    //   assert(m_level == MEM_L1);
    //   done_func(req);
    // } 
    if (m_level == MEM_L1) {
      done_func(req);
    } else if (m_level == MEM_L2) {
      req->m_dirty = false;
      m_prev_d->fill(req);
    }
  }
  // 3. Read(IF) or Write Miss => out_queue
  else {
    m_out_queue->push(req);
  }
} 

/** 
 * This function processes the output queue.
 * The function pops the requests from out_queue and accesses the next-level's cache or main memory.
 * CURRENT: There is no limit on the number of requests we can process in a cycle.
 */
void cache_c::process_out_queue() {
  auto it = m_out_queue->m_entry.begin();
  if (it == m_out_queue->m_entry.end()) return;
  mem_req_s* req = (*it);

  m_out_queue->pop(req);


  // TODO: in_flight_wb_queue 처리하기

  // Not all requests are miss
  // Type: Read(IF) or Write => Read miss or Write miss
  // Type: Writeback => Not a miss. Just write-back

  if (req->m_type == REQ_WB) {

    if (m_level == MEM_L1) {
      m_next->fill(req);
    } else if (m_level == MEM_L2) {
      // FIXME: WB to dram is using access()?
      m_memory->access(req);
    }

  } else if (req->m_type == REQ_DFETCH || req->m_type == REQ_DSTORE || req->m_type == REQ_IFETCH ) { // miss
  // access request to lower level  
    if (m_level == MEM_L1) {
      // If Write miss, (The fact that req is in out_queue means it is a miss)
      // move to lower input queue as "Read" Request
      req->m_is_write_miss = (req->m_type == REQ_DSTORE);

      m_next->access(req);
    } else if (m_level == MEM_L2) {
      if (req->m_type == REQ_DSTORE && (!req->m_is_write_miss)) {
        assert(false && "L2 Write Miss but type m_is_write_miss false");
        std::cout << "L2 Write Miss but type m_is_write_miss false" << std::endl;
      }
      // if (req->m_is_write_miss) {
        // std::cout << "L2 process_out_queue: Write Miss and access to memory" << std::endl;
      // }
      m_memory->access(req);
    }
  }
  else {
    assert(false);
  }
}

/** 
 * This function processes the fill queue.  The fill queue contains both the
 * data from the lower level and the dirty victim from the upper level.
 */

void cache_c::process_fill_queue() {
  // \TODO: Implement this function

  auto it = m_fill_queue->m_entry.begin();
  if (it == m_fill_queue->m_entry.end()) return;
  mem_req_s* req = (*it);

  // before ready cycle
  // yet finishing access() function
  if (req->m_rdy_cycle > m_cycle) {
    return;
  }
  m_fill_queue->pop(req);

  // Fill_1. The dirty victim from upper level for write-back: Fill_1
  //    - Do not modify the LRU stack 
  //    - clean victim never put into fill_queue
  // Fill_2. Due to Read(Write) miss, the data from lower level for fill

  // Fill_1
  if (req->m_type == REQ_WB) {
    assert(m_level == MEM_L2);
    // WriteBack to current cache
    cache_base_c::access(req->m_addr, WRITE_BACK, true);
    // Pop WB request from uppder level m_in_flight_wb_queue 
    m_in_flight_wb_queue->pop(req);
    // FIXME: L2에서 WB하고 자기꺼 pop, dram은 알아서 해주는 거겠지?
  }
  // Fill_2
  else {
    if (m_level == MEM_L1) {
      cache_base_c::access(req->m_addr, req->m_type, true);
      // if dirty victim has evicted, then write-back to L2
      if (get_is_evicted_dirty()) {
        // mem_req_s* wb_req = new
        addr_t index = (req->m_addr / m_line_size) % m_num_sets;
        addr_t offset = req->m_addr % m_line_size;
        addr_t wb_req_addr = get_evicted_tag() * m_num_sets * m_line_size + index * m_line_size + offset;
        mem_req_s* wb_req = new mem_req_s(wb_req_addr, REQ_WB);
        wb_req->m_id = 424; // WB request from L1 to L2
        wb_req->m_in_cycle = m_cycle;
        wb_req->m_rdy_cycle = m_cycle;
        wb_req->m_done = false;
        wb_req->m_dirty = true;
        wb_req->m_is_write_miss = false;

        m_wb_queue->push(wb_req);
        m_next->m_in_flight_wb_queue->push(wb_req);
      }

      done_func(req);

    } else if (m_level == MEM_L2) { // Read(Write) Miss and filled from memory
      // std::cout << req->m_type << std::endl;
      // std::cout << req->m_is_write_miss << std::endl;
      if (req->m_type != WRITE && req->m_is_write_miss) {
        std::cout << "write miss error" << std::endl;
      }
      // assert(req->m_type == WRITE && (!req->m_is_write_miss) && "If L2 fill_2, always write miss when write");
      if (req->m_is_write_miss ) {
        if (req->m_type != WRITE) {
          std::cout<<"If L2 fill_2, always write miss when write"<<std::endl;
          assert(false);
        }
      }
      // if (req->m_is_write_miss) {
      //   std::cout<<"L2 fill_2, write miss"<<std::endl;
      // }
      // First of all, forward to L1 
      m_prev_d->fill(req);

      // Write miss -> read
      // others -> others
      // int l2_req_type = (req->m_is_write_miss) ? READ : req->m_type;

      cache_base_c::access(req->m_addr, READ, true);

      // if dirty victim has evicted, then write-back to MEM
      if (get_is_evicted_dirty()) {
        addr_t index = (req->m_addr / m_line_size) % m_num_sets;
        addr_t offset = req->m_addr % m_line_size;
        assert(get_evicted_tag() != 0);
        addr_t wb_req_addr = get_evicted_tag() * m_num_sets * m_line_size + index * m_line_size + offset;
        mem_req_s* wb_req = new mem_req_s(wb_req_addr, REQ_WB);
        wb_req->m_id = 4240424; // WB request from L2 to MEM
        wb_req->m_in_cycle = m_cycle;
        wb_req->m_rdy_cycle = m_cycle;
        wb_req->m_done = false;
        wb_req->m_dirty = true;
        wb_req->m_is_write_miss = false;

        m_wb_queue->push(wb_req);
        m_memory->m_in_flight_wb_queue->push(wb_req);
      }

      /**
       * Back Invalidation Process
       * 1. if evicted (both clean victim and dirty victim)
       * 2. check if the evicted block is also in L1
       * 3. then 
       * 3-1. invalidate
       * 3-2. update LRU logic
       * 4. check if invalidated block is dirty
       * 4-1. if dirty, write-back to memory directly
       */
      if (get_is_evicted()) {
        // check whether evicted block is also in L1
        addr_t index = (req->m_addr / m_line_size) % m_num_sets;
        addr_t offset = req->m_addr % m_line_size;
        assert(get_evicted_tag() != 0);
        addr_t evicted_addr = get_evicted_tag() * m_num_sets * m_line_size + index * m_line_size + offset;
        bool exist_also_l1 = m_prev_d->cache_base_c::access(evicted_addr, CHECK, false);

        // 3. invalidate
        if (exist_also_l1) {
          m_prev_d->back_inv(evicted_addr);
        }
      }
    }
  }
}

/**
 * Back Invalidation Process
 * 3. then 
 * 3-1. invalidate
 * 3-2. update LRU logic
 * 4. check if invalidated block is dirty
 * 4-1. if dirty, write-back to memory directly
 */
void cache_c::back_inv(addr_t back_inv_addr) {
  assert (m_level == MEM_L1);

  ++m_num_backinvals;

  int tag = back_inv_addr / (m_num_sets * m_line_size);
  int set_index = (back_inv_addr / m_line_size) % m_num_sets;

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

  if (hit) {
    // if dirty, write back to memory directly
    if (set->m_entry[hit_index].m_dirty) {
      ++m_num_writebacks_backinval;
      // write back to memory directly
      mem_req_s* mem_wb_req = new mem_req_s(back_inv_addr, REQ_WB);
      mem_wb_req->m_id = 1537; // Direct WB_backinv request from L2 to MEM
      mem_wb_req->m_in_cycle = m_cycle;
      mem_wb_req->m_rdy_cycle = m_cycle;
      mem_wb_req->m_done = false;
      mem_wb_req->m_dirty = true;
      mem_wb_req->m_is_write_miss = false;
      m_memory->access(mem_wb_req);
    }

    // invalid
    set->m_entry[hit_index].m_valid = false;
    set->m_entry[hit_index].m_dirty = false;
    set->m_entry[hit_index].m_tag = 0;

    // update LRU
    set->m_lru_stack.remove(&set->m_entry[hit_index]);
  }
  // never goes into this
  else {
    assert(false);
  }
}

/** 
 * This function processes the write-back queue.
 * The function basically moves the requests from wb_queue to out_queue.
 * CURRENT: There is no limit on the number of requests we can process in a cycle.
 */
void cache_c::process_wb_queue() {
  // \TODO: Implement this function
  // auto it = m_wb_queue->m_entry.begin();
  // if (it == m_wb_queue->m_entry.end()) return;
  // mem_req_s* req = (*it);
  // m_wb_queue->pop(req);

  // // move to output queue
  // m_out_queue->push(req);
  while (!m_wb_queue->empty())
  {
    mem_req_s *req = m_wb_queue->m_entry.front();
    m_wb_queue->pop(req);
    m_out_queue->push(req);
  }
}

/**
 * Print statistics (DO NOT CHANGE)
 */
void cache_c::print_stats() {
  cache_base_c::print_stats();
  std::cout << "number of back invalidations: " << m_num_backinvals << "\n";
  std::cout << "number of writebacks due to back invalidations: " << m_num_writebacks_backinval << "\n";
}


/*
// If Dram forward cache block to L2, 
    // then also forward cache block to L1.
    // This is because of inclusive cache.
    if (m_level == MEM_L2) {
      m_prev_d->fill(req);
    }

    // We have to fill this cache as Read request
    // when originally Write Miss && current level is L2
    int req_type = (req->m_is_write_miss && m_level == MEM_L2) ? REQ_DFETCH : req->m_type;
    bool hit = cache_base_c::access(req->m_addr, req_type, true);

    // If the evicted cache line is dirty, we need to write-back to the lower level.
    // 1. Thus we have to create new request with evicted cache line
    // 2. And push it to WB queue
    if (get_is_evicted_dirty()) {
      assert(!hit && "cache_c::process_fill_queue(): All req of fill_queue should cache_base_c::access false");
      mem_req_s* wb_req = create_wb_req(get_evicted_tag(), req);

      m_wb_queue->push(wb_req);

      // put wb request into next cache(dram) in-flight wb queue
      // This request will be committed with next request.
      // FIXME: L1 -> L2 에 넣어준다. L2 -> dram에 넣어준다.
      if (m_level == MEM_L1) {
        m_next->m_in_flight_wb_queue->push(wb_req);
      } else if (m_level == MEM_L2) {
        m_memory->m_in_flight_wb_queue->push(wb_req);
      } else {
        assert(false && "cache_c::process_fill_queue(): Invalid m_level");
      }
    }

    // At L2 cache, if evicted, we should check back-Invalidation for inclusive cache
    // handling at L1 cache.
    if (get_is_evicted() && m_level == MEM_L2) {
      mem_req_s* back_inv_req = create_wb_req(get_evicted_tag(), req);
      m_prev_d->back_inv(back_inv_req);
    }
    // done only when L1 cache
    if (m_level == MEM_L1) {
      done_func(req);  
    } 
*/

// TODO
// check whether corresponding block is in this cache or not.
// void cache_c::back_inv(mem_req_s *back_inv_req) {

//   assert(m_level == MEM_L1 && "cache_c::back_inv(): This function should be called only in L1 cache");

//   addr_t address = back_inv_req->m_addr;
//   // std::cout<<"m_num_sets: "<<m_num_sets<<std::endl;
//   // std::cout<<"m_line_size: "<<m_line_size<<std::endl;
//   int tag = address / (m_num_sets * m_line_size);
//   int set_index = (address / m_line_size) % m_num_sets;

//   cache_set_c* set = m_set_list[set_index];

//   // Check if there is a cache hit
//   bool hit = false;
//   int hit_index = -1;

//   for (int i = 0; i < set->m_assoc; ++i) {
//     if (set->m_entry[i].m_valid && set->m_entry[i].m_tag == tag) {
//       hit = true;
//       hit_index = i;
//       break;
//     }
//   }

//   if (!hit) { 
//     // std::cout << "Inside the back_inv, just return" << std::endl;
//     return ; 
//   } // evicted cache block of L2 does not exist in L1.

//   // Back-Invalidate //
//   ++m_num_backinvals;
  
//   // invalidate
//   set->m_lru_stack.remove(&set->m_entry[hit_index]);
//   set->m_entry[hit_index].m_valid = false;
  
//   // If the evicted cache line is dirty, we need to write-back to the memory.
//   bool is_dirty = set->m_entry[hit_index].m_dirty;

//   // Write-back due to Back-Invalidate
//   if (is_dirty) {
//     ++m_num_writebacks_backinval;

//     // Write-back to dram directly, not push to fill queue
//     m_memory->access(back_inv_req);
//   }

// }


// mem_req_s* cache_c::create_wb_req(addr_t evicted_tag, mem_req_s* req) {
//   int set_index = (req->m_addr / m_line_size) % m_num_sets;
//   int offset = req->m_addr % m_line_size;
//   addr_t new_addr = (evicted_tag * set_index * m_line_size) + (set_index * m_line_size) + offset;

//   mem_req_s* new_req = new mem_req_s(new_addr, REQ_WB);
//   new_req->m_id = 424;
//   new_req->m_in_cycle = m_cycle;
//   new_req->m_rdy_cycle = m_cycle;
//   new_req->m_done = false;
//   new_req->m_dirty = true;
//   new_req->m_is_write_miss = false;

//   return new_req;
// }