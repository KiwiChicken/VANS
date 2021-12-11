#include "config.h"
#include "general/factory.h"
#include "general/trace.h"
#include <iostream>
#include <string>
#include <unistd.h>
#include<stdio.h>
#include<stdlib.h>
using namespace std;

int main(int argc, char *argv[])
{
    //string trace_filename;
    string config_filename;
    int num_thread, num_read;

    int c;
    while (-1 != (c = getopt(argc, argv, "c:n:r:"))) {
        switch (c) {
        case 'c':
            config_filename = optarg;
            break;
        case 'n':
            num_thread = atoi(optarg);
            break;
	case 'r':
	    num_read = atoi(optarg);
	    break;
	default:
            cout << "Usage: "
                 << "-c cfg_filename -n thread_count -r read_thread_count" << endl;
            return 0;
        }
    }

    std::cout << "thread count: " << num_thread << "\n";

    auto cfg   = vans::root_config(config_filename);
    auto model = vans::factory::make(cfg);
    vans::trace::run_trace(cfg, num_thread, num_read, model);

    return 0;
}
