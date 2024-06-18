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
  bool hit = cache_base_c::access(req->m_addr, req->m_type, false, true);

  // 1. Read(IF) Hit
  // 1.1 (L1 Cache)   => Done
  // 1.2 (L2) => upper level fill queue
  // 2. Write Hit => Done
  // 3. Read(IF) or Write Miss => out_queue

  // Cache hit
  if (hit) {
    // 1. Read(IF) Hit
    if (req->m_type == REQ_IFETCH || req->m_type == REQ_DFETCH) {
      // 1.1 (L1 Cache) Read Hit => Done
      if (m_level == MEM_L1) {
        // Done
        done_func(req);
      }
      // 1.2 (L2) => upper level fill queue
      else {
        // Fill the upper level cache
        // TODO: Implement L2 for Part3
        // m_prev_d->fill(req);
      }
    } 
    // 2. Write Hit => Done
    else {
      done_func(req);
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

  // before ready cycle
  // yet finishing access() function
  if (req->m_rdy_cycle > m_cycle) {
    return;
  }

  m_out_queue->pop(req);

  // move to lower input queue as "Read" Request
  req->m_is_orig_wr = true;
  // TEMP: Remove
  // std::cout << "Is originally Write?: " << req->m_is_orig_wr << '\n';
  m_memory->access(req);
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

  // done with the fill request
  done_func(req);

  // 1. The dirty victim from upper level for write-back
  //    - Do not modify the LRU stack 
  //    - clean victim never put into fill_queue
  // TODO
  // 2. Due to Read(Write) miss, the data from lower level for fill
  // 3. Write-back due to back invalidation
  bool use_lru = !(req->m_type == REQ_WB); // when request is WB type, then do not use LRU
  bool hit = cache_base_c::access(req->m_addr, req->m_type, true, use_lru);

  // If the evicted cache line is dirty, we need to write-back to the lower level.
  // 1. Thus we have to create new request with evicted cache line
  // 2. And push it to WB queue
  if (get_is_evicted_dirty()) {
    assert(!hit && "cache_c::process_fill_queue(): All req of fill_queue should cache_base_c::access false");
    mem_req_s* wb_req = create_wb_req(get_evicted_tag(), req);

    m_wb_queue->push(wb_req);

    // put current request into in-flight wb queue
    // This request will be committed with next request.
    // m_in_flight_wb_queue->push(wb_req);
  }
}

mem_req_s* cache_c::create_wb_req(addr_t evicted_tag, mem_req_s* req) {
  int set_index = (req->m_addr / m_line_size) % m_num_sets;
  int offset = req->m_addr % m_line_size;
  addr_t new_addr = (evicted_tag * set_index * m_line_size) + (set_index * m_line_size) + offset;

  mem_req_s* new_req = new mem_req_s(new_addr, WRITE_BACK);
  new_req->m_id = 424;
  new_req->m_in_cycle = m_cycle;
  new_req->m_rdy_cycle = m_cycle;
  new_req->m_done = false;
  new_req->m_dirty = true;
  new_req->m_is_orig_wr = false;

  return new_req;
}

/** 
 * This function processes the write-back queue.
 * The function basically moves the requests from wb_queue to out_queue.
 * CURRENT: There is no limit on the number of requests we can process in a cycle.
 */
void cache_c::process_wb_queue() {
  // \TODO: Implement this function
  auto it = m_wb_queue->m_entry.begin();
  if (it == m_wb_queue->m_entry.end()) return;
  mem_req_s* req = (*it);
  m_wb_queue->pop(req);

  // move to output queue
  req->m_type = REQ_WB;
  m_out_queue->push(req);
}

/**
 * Print statistics (DO NOT CHANGE)
 */
void cache_c::print_stats() {
  cache_base_c::print_stats();
  std::cout << "number of back invalidations: " << m_num_backinvals << "\n";
  std::cout << "number of writebacks due to back invalidations: " << m_num_writebacks_backinval << "\n";
}
