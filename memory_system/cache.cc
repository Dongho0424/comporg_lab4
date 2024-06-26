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
  delete m_mm;
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
  // if (m_in_queue->empty())
    // return;
  while (!m_in_queue->empty()) { 
    // auto it = m_in_queue->m_entry.begin();
    // if (it == m_in_queue->m_entry.end()) return;
    // mem_req_s* req = (*it);
    mem_req_s* req = m_in_queue->m_entry.front();


    // before ready cycle
    // yet finishing access() function
    if (req->m_rdy_cycle > m_cycle) {
      return;
    }

    m_in_queue->pop(req);
    
    int access_type = req->m_type; 
    
    // Write Miss at L2, then read access
    if(m_level == MEM_L2 && access_type == WRITE){
      access_type = READ;
    }
    bool hit = cache_base_c::access(req->m_addr, access_type, false);

    // 1. Read(IF) Hit
    // 1.1 (L1 Cache)   => Done
    // 1.2 (L2) => upper level fill queue
    // 2. Write Hit => Done
    // 3. Read(IF) or Write Miss => out_queue

    // Cache hit
    if (hit) {
      if (m_level == MEM_L1) {
        done_func(req);
      } else if (m_level == MEM_L2) {
        req->m_dirty = false;
        if (req->m_type == REQ_DFETCH || req->m_type == REQ_DSTORE) {
          m_prev_d->fill(req);
        }
        else if (req->m_type == REQ_IFETCH) {
          m_prev_i->fill(req);
        }
      }
    }
    // 3. Read(IF) or Write Miss => out_queue
    /** 
      * When cases of misses at L1 cache out_queue, check current req already exists in in_flight_reqs
      * If so, handling in order to get rid of duplicated requests
      * Both req of in_flight_reqs and current is miss and we have to mark whether miss or not
      * | in_flight_reqs | current | handling 
      * 1. R | R | common
      * 2. I | I | common
      * 3. W | R | common 
      * 4. W | W | common, (1) L1 num_writes ++
      * 5. R | W | common, (1) L1 num_writes ++, (2) change access_type R -> W of in_flight_reqs
      * < common >
      * 1. do not forward out_queue
      * 2. delete req
      * 3. L1 num_hits++
      * 4. do not change LRU
      */ 
    else {
      if (m_level == MEM_L1){
        // above situation occurs.
        assert (m_mm != nullptr);
        if (!m_mm->m_in_flight_reqs.empty()) {
          if (m_mm->is_repeated_miss_req(req)) {
          
            // common 3: L1 num_hits++
            // common 4: do not change LRU
            m_num_hits++;

            int access_type_of_in_flight_req = m_mm->get_access_type_of_in_flight_req(req);
            if ( (access_type_of_in_flight_req == READ && req->m_type == READ) ||
                 (access_type_of_in_flight_req == INST_FETCH && req->m_type == INST_FETCH) ||
                 (access_type_of_in_flight_req == WRITE && req->m_type == READ)
            ) {
              // 1. R | R
              // 2. I | I
              // 3. W | R
              // -> only common things
            } 
            else if ( (access_type_of_in_flight_req == WRITE && req->m_type == WRITE) ) {
              // 4. W | W
              m_num_writes++;
            } 
            else if ( (access_type_of_in_flight_req == READ && req->m_type == WRITE) ) {
              // 5. R | W
              m_num_writes++;
              m_mm->set_access_type_of_in_flight_req(req, WRITE);
            }
            else {
              assert(false);
            }

            // common 2: delete req
            done_func(req);
            // delete req;

            // commmon 1: do not forward out_queue
            continue;
          }
          // Nope. Just pure read or write miss
          // Forward to out_queue with missing mark.
          else {
            req->m_is_miss = true;
            m_out_queue->push(req);
          }
        }
        // Nope. Just pure read or write miss
        // Forward to out_queue with missing mark.
        else {
          req->m_is_miss = true;
          m_out_queue->push(req);
        }
      }
      else { // L2 Cache: just forwarding to out_queue
        m_out_queue->push(req);
      }
    }
  }
} 

/** 
 * This function processes the output queue.
 * The function pops the requests from out_queue and accesses the next-level's cache or main memory.
 * CURRENT: There is no limit on the number of requests we can process in a cycle.
 */
void cache_c::process_out_queue() {
  // if (it == m_out_queue->m_entry.end()) return;
  while (!m_out_queue->empty()) {
    // auto it = m_out_queue->m_entry.begin();
    // mem_req_s* req = (*it);
    mem_req_s* req = m_out_queue->m_entry.front();

    m_out_queue->pop(req);

    // Not all requests are miss
    // Type: Read(IF) or Write => Read miss or Write miss
    // Type: Writeback => Not a miss. Just write-back

    if (req->m_type == REQ_WB) {

      if (m_level == MEM_L1) {
        m_next->fill(req);
      } else if (m_level == MEM_L2) {
        m_memory->access(req);
      }

    } else if (req->m_type == REQ_DFETCH || req->m_type == REQ_DSTORE || req->m_type == REQ_IFETCH ) { // miss
    // access request to lower level  
      if (m_level == MEM_L1) {
        
        m_next->access(req);
      } else if (m_level == MEM_L2) {
        
        m_memory->access(req);
      }
    }
    else {
      assert(false);
    }
  }
}

/** 
 * This function processes the fill queue.  The fill queue contains both the
 * data from the lower level and the dirty victim from the upper level.
 */

void cache_c::process_fill_queue() {
  // if (it == m_fill_queue->m_entry.end()) return;

  // if (m_fill_queue->empty())
    // return;
  while (!m_fill_queue->empty()) {   
  // auto it = m_fill_queue->m_entry.begin();
  // mem_req_s* req = (*it);
  mem_req_s* req = m_fill_queue->m_entry.front(); 

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
  }
  // Fill_2
  else {
    if (m_level == MEM_L1) {
      cache_base_c::access(req->m_addr, req->m_type, true);
      // if dirty victim has evicted, then write-back to L2
      if (get_is_evicted_dirty()) {
        // all L1I cache entry must be clean. 
        assert (req->m_type != REQ_IFETCH);

        // mem_req_s* wb_req = new
        addr_t index = (req->m_addr / m_line_size) % m_num_sets;
        // addr_t offset = req->m_addr % m_line_size;
        // addr_t wb_req_addr = get_evicted_tag() * m_num_sets * m_line_size + index * m_line_size + offset;
        addr_t wb_req_addr = get_evicted_addr();
        mem_req_s* wb_req = new mem_req_s(wb_req_addr, REQ_WB);
        wb_req->m_id = 424; // WB request from L1 to L2
        wb_req->m_in_cycle = m_cycle;
        wb_req->m_rdy_cycle = m_cycle;
        wb_req->m_done = false;
        wb_req->m_dirty = true;

        m_wb_queue->push(wb_req);
        m_next->m_in_flight_wb_queue->push(wb_req);
      }

      done_func(req);

    } else if (m_level == MEM_L2) { // Read(Write) Miss and filled from memory
      
      // First of all, forward to L1 
      if (req->m_type == REQ_DFETCH || req->m_type == REQ_DSTORE) {
        m_prev_d->fill(req);
      }
      else if (req->m_type == REQ_IFETCH) {
        m_prev_i->fill(req);
      }

      int access_type = req->m_type; 
  
      // Write Miss Fill at L2, then read access
      if(access_type == WRITE){
        access_type = READ;
      }
      cache_base_c::access(req->m_addr, access_type, true);

      // if dirty victim has evicted, then write-back to MEM
      if (get_is_evicted_dirty()) {

        addr_t wb_req_addr = get_evicted_addr();
        mem_req_s* wb_req = new mem_req_s(wb_req_addr, REQ_WB);
        wb_req->m_id = 4240424; // WB request from L2 to MEM
        wb_req->m_in_cycle = m_cycle;
        wb_req->m_rdy_cycle = m_cycle;
        wb_req->m_done = false;
        wb_req->m_dirty = true;

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
        addr_t evicted_addr = get_evicted_addr();
        bool exist_also_l1d = m_prev_d->cache_base_c::access(evicted_addr, CHECK, false);
        bool exist_also_l1i = m_prev_i->cache_base_c::access(evicted_addr, CHECK, false);

        // 3. invalidate - L1D
        if (exist_also_l1d) {
          m_prev_d->back_inv(evicted_addr, "L1D");
        }
        // 3. invalidate - L1I
        if (exist_also_l1i) {
          m_prev_i->back_inv(evicted_addr, "L1I");
        }
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
void cache_c::back_inv(addr_t back_inv_addr, std::string cache_info) {
  assert (m_level == MEM_L1);
  assert (cache_info == "L1D" || cache_info == "L1I");

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

  // all L1I cache entry must be clean. 
  if (cache_info == "L1I") {
    assert (!set->m_entry[hit_index].m_dirty);
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
