// ECE 430.322: Computer Organization
// Lab 4: Memory System Simulation

/**
 *
 * @class memory_hierarchy_c 
 *
 * This models the memory hierarchy in the processor. When the constructor is
 * called, the init() function initialize the cache/main memory components in
 * the memory hierarchy and connects them.  When the processor executes a
 * memory instruction, and it calls the access() function.
 */

#include "memory_hierarchy.h"
#include "cache.h"

#include <cassert>

memory_hierarchy_c::memory_hierarchy_c(config_c& config) {

  m_config = config;
  m_mem_req_id = 0;    // starting unique request id
  m_cycle = 0;         // memory hierarchy cycle

  m_l1u_cache = nullptr;
  m_l1i_cache = nullptr;                     
  m_l1d_cache = nullptr;                     
  m_l2_cache = nullptr;                     
  m_dram = nullptr;                     

  m_done_queue = new queue_c();

  init(config);

  // set done requests callback function for children.
  if (config.get_mem_hierarchy() == static_cast<int>(Hierarchy::DRAM_ONLY)) {
    assert(m_dram && "main memory is not instantiated");
    m_dram->set_done_func(std::bind(&memory_hierarchy_c::push_done_req, this, std::placeholders::_1)); 
  } else if (config.get_mem_hierarchy() == static_cast<int>(Hierarchy::SINGLE_LEVEL)) {
    assert(m_dram && "main memory is not instantiated");
    assert(m_l1d_cache && "l1d is not instantiated");
    m_dram->set_done_func([&](mem_req_s *req) { m_l1d_cache->fill(req); }); 
    m_l1d_cache->set_done_func(std::bind(&memory_hierarchy_c::push_done_req, this, std::placeholders::_1)); 
  } else if (config.get_mem_hierarchy() == static_cast<int>(Hierarchy::MULTI_LEVEL)) {
    assert(m_dram && "main memory is not instantiated");
    assert(m_l1u_cache && "l1u is not instantiated");
    assert(m_l1i_cache && "L1I cache is not instantiated");
    assert(m_l1d_cache && "L1D cache is not instantiated");
    assert(m_l2_cache && "l2 is not instantiated");
    m_l1u_cache->set_done_func(std::bind(&memory_hierarchy_c::push_done_req, this, std::placeholders::_1)); 
    m_l1i_cache->set_done_func(std::bind(&memory_hierarchy_c::push_done_req, this, std::placeholders::_1)); 
    m_l1d_cache->set_done_func(std::bind(&memory_hierarchy_c::push_done_req, this, std::placeholders::_1)); 
  }
   
}

/**
 * This initializes the memory hierarchy to simulate with a given configuration.
 */
void memory_hierarchy_c::init(config_c& config) {
  ////////////////////////////////////////////////////////////////////
  // TODO: Write the code to implement this function
  // 1. Instantiate caches and main memory (e.g., DRAM)
  // 2. Configure neighbors of each cache
  ////////////////////////////////////////////////////////////////////
  
  // instantiate caches and main memory (e.g., DRAM)
  m_dram = new simple_mem_c("DRAM", MEM_MC, config.get_memory_latency());

  int l1d_num_sets = config.get_l1d_size() / (config.get_l1d_assoc() * config.get_l1d_line_size());
  m_l1d_cache = new cache_c("L1D", MEM_L1, l1d_num_sets, config.get_l1d_assoc(), config.get_l1d_line_size(), config.get_l1d_latency());

  int l1i_num_sets = config.get_l1i_size() / (config.get_l1i_assoc() * config.get_l1i_line_size());
  m_l1i_cache = new cache_c("L1I", MEM_L1, l1i_num_sets, config.get_l1i_assoc(), config.get_l1i_line_size(), config.get_l1i_latency());

  // same as l1d
  m_l1u_cache = new cache_c("L1U", MEM_L1, l1d_num_sets, config.get_l1d_assoc(), config.get_l1d_line_size(), config.get_l1d_latency());

  int l1i_num_set=config.get_l1i_size() / (config.get_l1i_line_size() * config.get_l1i_assoc());
  m_l1i_cache= new cache_c("L1I", 1, l1i_num_set, config.get_l1i_assoc(), config.get_l1i_line_size(), config.get_l1i_latency());

  int l2_num_sets = config.get_l2_size() / (config.get_l2_assoc() * config.get_l2_line_size());
  m_l2_cache = new cache_c("L2", MEM_L2, l2_num_sets, config.get_l2_assoc(), config.get_l2_line_size(), config.get_l2_latency());

  (*m_l1d_cache).m_mm = this;
  (*m_l1u_cache).m_mm = this;
  (*m_l1i_cache).m_mm = this;
  (*m_l2_cache).m_mm = this;

  if (config.get_mem_hierarchy() == static_cast<int>(Hierarchy::DRAM_ONLY)) {
    m_dram->configure_neighbors(nullptr);
  } else if (config.get_mem_hierarchy() == static_cast<int>(Hierarchy::SINGLE_LEVEL)) {
    m_dram->configure_neighbors(m_l1d_cache);
    m_l1d_cache->configure_neighbors(nullptr, nullptr, nullptr, m_dram);
  } else if (config.get_mem_hierarchy() == static_cast<int>(Hierarchy::MULTI_LEVEL)) {
    m_l1i_cache->configure_neighbors(nullptr,nullptr,m_l2_cache,m_dram);
    m_l1d_cache->configure_neighbors(nullptr,nullptr,m_l2_cache,m_dram);
    m_l1u_cache->configure_neighbors(nullptr,nullptr,m_l2_cache,m_dram);

    // unified
    // m_l2_cache->configure_neighbors(nullptr, m_l1u_cache, nullptr, m_dram);

    // splited
    m_l2_cache->configure_neighbors(m_l1i_cache, m_l1d_cache, nullptr, m_dram);

    m_dram->configure_neighbors(m_l2_cache);
  }
}

/**
 * This creates a new memory request for the given memory address and accesses the top-level
 * memory components in the memory hierarchy (e.g., L1 or main memory). 
 */

bool memory_hierarchy_c::access(addr_t address, int access_type) {

  // create a memory request
  mem_req_s* req = create_mem_req(address, access_type);

  m_in_flight_reqs.push_back(req);

  ////////////////////////////////////////////////////////////////////
  // TODO: Write the code to implement this function
  // Access the top-level memory component
  ////////////////////////////////////////////////////////////////////

  if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::DRAM_ONLY)) {
    return m_dram->access(req);
  } else if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::SINGLE_LEVEL)) { 
    return m_l1d_cache->access(req);
  } else if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::MULTI_LEVEL)) { 
    // unified
    // return m_l1u_cache->access(req);
    
    // splited
    if(access_type == INST_FETCH){
       return m_l1i_cache->access(req);
    }
    else{
       return m_l1d_cache->access(req);
    }
  }
  return false;
}

/**
 * Create a new memory request that goes through memory hierarchy.  
 * @note You do not have to modify this (other than for debugging purposes).
 */
mem_req_s* memory_hierarchy_c::create_mem_req(addr_t address, int access_type) { 
  
  mem_req_s* req = new mem_req_s(address, access_type);

  req->m_id = m_mem_req_id++;
  req->m_in_cycle = m_cycle;
  req->m_rdy_cycle = m_cycle;
  req->m_done = false;
  req->m_dirty = false;

  // DEBUG("[MEM_H] Create REQ #%d %8lx @ %ld\n", req->m_id, req->m_addr, m_cycle);
  return req;
}

/**
 * Tick a cycle for memory hierarchy.
 */
void memory_hierarchy_c::run_a_cycle() {
  ////////////////////////////////////////////////////////////////////
  // \TODO: Write the code to implement this function
  // 1. Tick a acycle for each cache/memory component
  // Think carefully what should be the order of run_a_cycle
  // 2. Process done requests.
  ////////////////////////////////////////////////////////////////////

  if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::DRAM_ONLY)) {
    m_dram->run_a_cycle();
  } else if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::SINGLE_LEVEL)) { 
    m_l1d_cache->run_a_cycle();
    m_dram->run_a_cycle();
  } else if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::MULTI_LEVEL)) { 
    //splited
    m_l1i_cache->run_a_cycle();
    m_l1d_cache->run_a_cycle();

    //unified
    //m_l1u_cache->run_a_cycle();
    m_l2_cache->run_a_cycle();
    m_dram->run_a_cycle();
  } 

  process_done_req();

  ++m_cycle; 
}

/**
 * This function processes the done request. The done_queue contains the
 * requests whose data is ready to return to the core.  
 * Processing a "done request" means sending the data to the core (conceptually).
 */
void memory_hierarchy_c::process_done_req() {
  ////////////////////////////////////////////////////////////////////
  // TODO: Write the code to implement this function
  // Free done requests
  ////////////////////////////////////////////////////////////////////
  
  for (auto it = m_done_queue->m_entry.begin(); it != m_done_queue->m_entry.end(); ) {
    free_mem_req(*it);
    ++it;
  }
  m_done_queue->m_entry.clear();
}

/**
 * This function is called when we get the data for the memory request.
 */
void memory_hierarchy_c::free_mem_req(mem_req_s* req) {

  auto& vv = m_in_flight_reqs;
  vv.erase(std::remove(vv.begin(), vv.end(), req), vv.end());
  delete req;

#ifdef __DEBUG__
  //dump(false); // print out cache dump
#endif
}

/**
 * This function is called when the request is done and data is ready to return to the core.
 * This is called Tfrom the top-level memory component.
 */
void memory_hierarchy_c::push_done_req(mem_req_s* req) {
  DEBUG("[MEM_H] Done REQ #%d %8lx @ %ld\n", req->m_id, req->m_addr, m_cycle);
  m_done_queue->push(req);
}

/**
 * This function checks if all the in-flight writebacks are done. This is the point
 * where we finish up the simulation.
 */
bool memory_hierarchy_c::is_wb_done() {
  ////////////////////////////////////////////////////////////////////
  // TODO: Write the code to implement this function
  // If there is no in-flight writeback requests for all the caches and
  // main memory, return true.
  ////////////////////////////////////////////////////////////////////
  bool is_done = false;
  if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::DRAM_ONLY)) {
    is_done = m_dram->m_in_flight_wb_queue->empty();
  } else if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::SINGLE_LEVEL)) { 
    is_done = m_dram->m_in_flight_wb_queue->empty() && 
              m_l1d_cache->m_in_flight_wb_queue->empty();
  } else if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::MULTI_LEVEL)) { 
    // unified
    // is_done = m_dram->m_in_flight_wb_queue->empty() && 
    //           m_l1u_cache->m_in_flight_wb_queue->empty() && 
    //           m_l2_cache->m_in_flight_wb_queue->empty();
    // unified
    is_done = m_dram->m_in_flight_wb_queue->empty() && 
              m_l1i_cache->m_in_flight_wb_queue->empty() && 
              m_l1d_cache->m_in_flight_wb_queue->empty() && 
              m_l2_cache->m_in_flight_wb_queue->empty();
  }

  return is_done;
}

///////////////////////////////////////////////////////////////////////////////////////////////
memory_hierarchy_c::~memory_hierarchy_c() {
  if (m_l1u_cache) delete m_l1u_cache;
  if (m_l1i_cache) delete m_l1i_cache;
  if (m_l1d_cache) delete m_l1d_cache;
  if (m_l2_cache)  delete m_l2_cache;
  if (m_dram)      delete m_dram;
}

void memory_hierarchy_c::print_stats() {

  if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::SINGLE_LEVEL)) {
    m_l1d_cache->print_stats();
  } else if (m_config.get_mem_hierarchy() == static_cast<int>(Hierarchy::MULTI_LEVEL)) {
    // unified
    // m_l1u_cache->print_stats();

    // splited
    m_l1i_cache->print_stats();
    m_l1d_cache->print_stats();
    m_l2_cache->print_stats();
  }
}

void memory_hierarchy_c::dump(bool is_file) {
  if (m_l1u_cache) m_l1u_cache->dump_tag_store(is_file);
  if (m_l1i_cache) m_l1i_cache->dump_tag_store(is_file);
  if (m_l1d_cache) m_l1d_cache->dump_tag_store(is_file);
  if (m_l2_cache)  m_l2_cache->dump_tag_store(is_file);
}
