#include "trace.h"
#include "utils.h"
#include <chrono>
#include <stdlib.h>
#include <sstream>
#include <iomanip>

#include <mutex>
#include <memory>
#include <vector>
#include <thread>
#include <pthread.h>
#include <stdlib.h>
#include <linux/types.h>

using namespace std;
namespace vans::trace
{

int n_thread, n_read;
int r_cnt, w_cnt = 0;
std::mutex clkmutex;
//std::stringstream sstream;

bool get_dram_trace_request(logic_addr_t &addr,
                                   base_request_type &type,
                                   bool &critical,
                                   clk_t &idle_clk_injection)
{
    std::string line;
    /*do {
        getline(file, line);
    } while (line.rfind('#', 0) == 0);
    if (file.eof()) {
        return false;
    }*/
    //random addr; step of 64 bytes; max addr is 640,000 | 0x9C400
    int addr_int = (rand() % 10000) * 64;
    //sstream << std::hex << addr_int;
    std::stringstream stream;
    stream << "0x"
         << std::setfill ('0') << std::setw(8)
         << std::hex << addr_int;
    std::string addr_str = stream.str();
    //type
    if (n_read > 0) {
        line = addr_str + " R";
        r_cnt++;
    }
    else {
        line = addr_str + " W";
        w_cnt++;
    }
    //std::cout << line << std::endl;


    size_t pos;
    addr     = std::stoul(line, &pos, 16);
    critical = false;
    pos      = line.find_first_not_of(' ', pos + 1);

    if (pos == std::string::npos || line.substr(pos)[0] == 'R') {
        type     = base_request_type::read;
        critical = false;
    } else if (line.substr(pos)[0] == 'W') {
        type = base_request_type::write;
    } else if (line.substr(pos)[0] == 'C') {
        type     = base_request_type::read;
        critical = true;
    } else
        throw std::runtime_error("Trace file format error.");

    pos = line.find_first_not_of(':', pos + 1);
    if (pos != std::string::npos) {
        idle_clk_injection = std::stoul(line.substr(pos));
    } else {
        idle_clk_injection = clk_invalid;
    }
    return true;
}

void run_trace(root_config &cfg, int num_thread, int num_read, std::shared_ptr<base_component> model)
{
    int access_count = 0;
    int access_end = 1000 * 10; //0.1m requests
    n_thread = num_thread;
    n_read = num_read;
    //trace trace(trace_filename);
    bool stall               = false;
    bool trace_end           = false;
    bool critical_stall      = false;
    bool critical_load       = false;
    bool wait_idle_clk       = false;
    auto heart_beat_epoch    = cfg["trace"].get_ulong("heart_beat_epoch");
    auto report_epoch        = cfg["trace"].get_ulong("report_epoch");
    clk_t idle_clk_injection = clk_invalid;
    double tCK               = std::stod(cfg["basic"]["tCK"]);

    counter cnt_events("vans", "run_trace", {"write_access", "read_access", "total"});
    size_t tail_latency_cnt = 0;

    auto critical_read_callback = [&critical_stall](logic_addr_t logic_addr, clk_t curr_clk) {
        critical_stall = false;
    };
    auto tail_latency_callback = [&](logic_addr_t logic_addr, clk_t curr_clk) {
        tail_latency_cnt++;
        if (logic_addr % 256 == 0)
            std::cout << "[" << tail_latency_cnt << "]:" << curr_clk << std::endl;
    };
    auto normal_read_callback = [&](logic_addr_t logic_addr, clk_t curr_clk) {};

    base_callback_f callback = normal_read_callback;
    if (cfg["trace"].get_ulong("report_tail_latency") != 0) {
        callback = tail_latency_callback;
        std::cout << "Report tail latency" << std::endl;
    }

    logic_addr_t addr      = 0;
    clk_t curr_clk         = 0;
    clk_t last_trace_clk   = 0;
    base_request_type type = base_request_type::read;
    base_request req(type, addr, curr_clk, callback);

    auto sim_start = std::chrono::high_resolution_clock::now();
    
    vector<unique_ptr<thread>> workers(num_thread);
    for (int t = 0; t<num_thread; t++) { // Spawn threads
      workers[t] = make_unique<thread>([&, t]() {
    
    while (access_count < access_end) {
        if (!wait_idle_clk) {
            if (access_count < access_end&& !stall && !critical_stall) {
                access_count++;

                get_dram_trace_request(addr, type, critical_load, idle_clk_injection);

                if (idle_clk_injection != clk_invalid)
                    wait_idle_clk = true;
            }

            if (access_count < access_end) {
                req.addr = addr;
                req.type = type;
                if (critical_load) {
                    req.callback = critical_read_callback;
                } else {
                    req.callback = callback;
                }

                if (!critical_stall) {
                    auto [issued, deterministic, next_clk] = model->issue_request(req);
                    stall                                  = !issued;
                    if (issued) {
                        if (type == base_request_type::read) {
                            cnt_events["read_access"]++;
                        } else if (type == base_request_type::write) {
                            cnt_events["write_access"]++;
                        }

                        if (critical_load) {
                            critical_stall = true;
                        }
                        cnt_events["total"]++;
                        if (report_epoch != 0 && cnt_events["total"] % report_epoch == 0) {
                            printf("Trace No. %lu type %d addr 0x%lx arrived at clock %lu\n",
                                   cnt_events["total"],
                                   int(type),
                                   addr,
                                   curr_clk);
                        }
                    }
                }
            } else {
                last_trace_clk = curr_clk;
            }
        } else {
            if (idle_clk_injection > 0) {
                idle_clk_injection--;
            } else {
                wait_idle_clk = false;
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(clkmutex);
        model->tick(curr_clk);
        curr_clk++;
        }

        if (heart_beat_epoch != 0 && curr_clk % heart_beat_epoch == 0) {
            std::cout << "Trace heart beat: " << curr_clk << std::endl;
        }
    }
    return;
    });
    }
    
    for (auto &worker : workers) {
      worker->join();
   }
    
    model->drain();

    while (model->pending()) {
        model->tick(curr_clk);
        curr_clk++;
        if (heart_beat_epoch != 0 && curr_clk % heart_beat_epoch == 0) {
            std::cout << "Trace heart beat: " << curr_clk << std::endl;
        }
    }

    auto sim_end      = std::chrono::high_resolution_clock::now();
    auto sim_duration = std::chrono::duration_cast<std::chrono::seconds>(sim_end - sim_start).count();

    model->print_counters();

    std::cout << "Total clock: " << curr_clk << std::endl;
    std::cout << "Last command clock: " << last_trace_clk << std::endl;
    std::cout << "Total ns: " << std::fixed << double(curr_clk) * tCK << std::endl;
    std::cout << "Last command ns: " << std::fixed << double(last_trace_clk) * tCK << std::endl;
    std::cout << "Simulation time: " << sim_duration << " secs" << std::endl;
    
    std::cout << "Total read  bw: " << std::fixed << double(r_cnt * 64) / (double(curr_clk) * tCK  / (1000.0 * 1000.0 * 1000.0)) / 1024 / 1024 /1024 << "GB/s" << std::endl;
    std::cout << "Total write  bw: " << std::fixed << double(w_cnt * 64) / (double(curr_clk) * tCK / (1000.0 * 1000.0 * 1000.0)) / 1024 / 1024 /1024 << "GB/s" << std::endl;
}

} // namespace vans::trace

