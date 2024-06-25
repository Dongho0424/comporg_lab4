// ECE 430.322: Computer Organization
// Lab 4: Memory System Simulation

#ifndef __MEMORY_HIERARCHY_H__
#define __MEMORY_HIERARCHY_H__

#include "atom/mem_req.h"
#include "memory_controller/simple_mem.h"
#include "cache.h"
#include "config.h"

#include <vector>

enum class Hierarchy {
  DRAM_ONLY,
  SINGLE_LEVEL,
  MULTI_LEVEL
};

// forward declaration
class cache_c;
class simple_mem_c;

class memory_hierarchy_c {
public:
  memory_hierarchy_c(config_c& config);       
  ~memory_hierarchy_c();         

  void init(config_c& config);                 ///< initialize memory hierarchy
  bool access(addr_t addr, int access_type);   ///< access function
  void run_a_cycle();                          ///< tick a cycle

  config_c m_config;
  
  friend class cache_c;

  /// @brief if the request is repeated and miss, return true
  /// @param req 
  /// @return 
  bool is_repeated_miss_req(mem_req_s* req) {
    auto it = std::find_if(m_in_flight_reqs.begin(), m_in_flight_reqs.end(),
                           [&](mem_req_s *r) { return r->m_addr == req->m_addr && r->m_is_miss; });
    return it != m_in_flight_reqs.end();
  }

  /// @brief If the req is repeated and miss, return access type
  /// @param req 
  /// @return 
  int get_access_type_of_in_flight_req(mem_req_s* req) {
    auto it = std::find_if(m_in_flight_reqs.begin(), m_in_flight_reqs.end(),
                           [&](mem_req_s *r) { return r->m_addr == req->m_addr && r->m_is_miss; });
    if (it != m_in_flight_reqs.end()) {
        return (*it)->m_type;
    }
    return -1; // Return -1 or another appropriate value to indicate not found/error
  }

  void set_access_type_of_in_flight_req(mem_req_s* req, int access_type) {
    auto it = std::find_if(m_in_flight_reqs.begin(), m_in_flight_reqs.end(),
                           [&](mem_req_s *r) { return r->m_addr == req->m_addr && r->m_is_miss; });
    if (it != m_in_flight_reqs.end()) {
        (*it)->m_type = access_type;
    }
  }
                                               
private:
  mem_req_s* create_mem_req(addr_t address, int access_type);
  void free_mem_req(mem_req_s* req);

  counter m_mem_req_id;                        ///< memory request id to assign
  simple_mem_c* m_dram;                        ///< simple main memory
  counter m_cycle;                             ///< clock cycle
                                               
public:
  void dump(bool is_file);                     ///< dump the data in cache after simulation

  void process_done_req();
  void push_done_req(mem_req_s* req);
  bool is_wb_done();
  void print_stats();
  int  get_num_in_flight_reqs(void) { return m_in_flight_reqs.size(); }
                                              
private:
  cache_c* m_l1u_cache;                        ///< l1u_cache for unified I/D
  cache_c* m_l1i_cache;                        ///< l1i_cache
  cache_c* m_l1d_cache;                        ///< l1d_cache 

  cache_c* m_l2_cache;                         ///< l2_cache
                                               
  std::vector<mem_req_s*> m_in_flight_reqs;    ///< memory requests in the memory hierarchy
  queue_c* m_done_queue;                       ///< holds the requests that are done (i.e., data ready for the core)
};

#endif // !__MEMORY_HIERARCHY_H__
