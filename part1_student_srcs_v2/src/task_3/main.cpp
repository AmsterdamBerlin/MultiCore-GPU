#include "systemc.h"
#include "aca2009.h"
#include "core.cpp"

int sc_main(int argc, char* argv[])
{
    try
    {
        // Get the tracefile argument and create Tracefile object
        // This function sets tracefile_ptr and num_cpus
        init_tracefile(&argc, &argv);

        // Initialize statistics counters
        stats_init();

        cout << "Number of CPUs: " << num_cpus << endl;

        // Instantiate Modules
        Bus     bus("bus");
        Cache* cache[num_cpus];
        CPU*    cpu[num_cpus];

        // Signals
        sc_buffer<Cache::Function> *sigMemFunc = new sc_buffer<Cache::Function>[num_cpus];
        sc_buffer<Cache::RetCode>  *sigMemDone = new sc_buffer<Cache::RetCode>[num_cpus];
        sc_signal<int>              *sigMemAddr = new sc_signal<int>[num_cpus];
        sc_signal_rv<8>             *sigMemData = new sc_signal_rv<8>[num_cpus];

        // Signals Cache-Bus
        sc_signal<int>              sigBusWriter;
        sc_signal<int> sigBusValid;

        // Signals for waveforms
        // hit: clock, address, set number, line number
        sc_signal<uint8_t>  *sigSet = new sc_signal<uint8_t>[num_cpus];
        sc_signal<uint8_t>  *sigLine = new sc_signal<uint8_t>[num_cpus];
        sc_signal<bool>     *sigHit = new sc_signal<bool>[num_cpus];
        sc_signal<bool>     *sigWR = new sc_signal<bool>[num_cpus];
        // miss: line number to be replaced
        // hit/miss, read/write


        // The clock that will drive the CPU and Cache
        sc_clock clk;

        bus.Port_CLK(clk);
        bus.Port_BusWriter(sigBusWriter);
        bus.Port_BusReq(sigBusValid);

        /* Create and connect all caches and cpu's. */
        for(int i = 0; i < num_cpus; i++)
        {
            /* Each process should have a unique string name. */
            char name_cache[12];
            char name_cpu[12];

            /* Use number in unique string name. */
            //name_cache << "cache_" << i;
            //name_cpu   << "cpu_"   << i;

            sprintf(name_cache, "cache_%d", i);
            sprintf(name_cpu, "cpu_%d", i);

            /* Create CPU and Cache. */
            cache[i] = new Cache(name_cache);
            cpu[i] = new CPU(name_cpu);

            /* Set ID's. */
            cpu[i]->cpu_id = i;
            cache[i]->cache_id = i;
            //cache[i]->snooping = snooping;

            /* Cache to Bus. */
            cache[i]->Port_BusAddr(bus.Port_BusAddr);
            cache[i]->Port_BusWriter(sigBusWriter);
            cache[i]->Port_BusReq(sigBusValid);
            cache[i]->Port_Bus(bus);

            /* Cache to CPU. */
            cache[i]->Port_Func(sigMemFunc[i]);
            cache[i]->Port_Addr(sigMemAddr[i]);
            cache[i]->Port_Data(sigMemData[i]);
            cache[i]->Port_Done(sigMemDone[i]);

            /* CPU to Cache. */
            cpu[i]->Port_MemFunc(sigMemFunc[i]);
            cpu[i]->Port_MemAddr(sigMemAddr[i]);
            cpu[i]->Port_MemData(sigMemData[i]);
            cpu[i]->Port_MemDone(sigMemDone[i]);

            cache[i]->Set_No(sigSet[i]);
            cache[i]->Line_No(sigLine[i]);
            cache[i]->Hit_Point(sigHit[i]);
            cache[i]->Write_Read(sigWR[i]);

            /* Set Clock */
            cache[i]->Port_CLK(clk);
            cpu[i]->Port_CLK(clk);
        }

        // Open VCD file
        sc_trace_file *wf = sc_create_vcd_trace_file("final_cache");
        sc_trace(wf, clk, "clock");
        sc_trace(wf, sigBusValid, "bus_request");
        sc_trace(wf, sigBusWriter, "bus_writer");

        for(int i = 0; i < num_cpus; i++){
            char name_memaddr[12];
            char name_set[12];
            char name_line[12];
            char name_hitmiss[12];
            char name_writeread[14];

            sprintf(name_memaddr, "address(%d)", i);
            sprintf(name_set, "set_num(%d)", i);
            sprintf(name_line, "line_num(%d)", i);
            sprintf(name_hitmiss, "hit/miss(%d)", i);
            sprintf(name_writeread, "write/read(%d)", i);

            // Dump the desired signals
            sc_trace(wf, sigHit[i], name_hitmiss);
            sc_trace(wf, sigMemAddr[i], name_memaddr);
            sc_trace(wf, sigSet[i], name_set);
            sc_trace(wf, sigLine[i], name_line);
            sc_trace(wf, sigWR[i], name_writeread);
        }

        cout << "Running (press CTRL+C to interrupt)... " << endl;

        // Start Simulation
        sc_start();

        // Print statistics after simulation finished
        stats_print();
        sc_close_vcd_trace_file(wf);
        return 0;
    }

    catch (exception& e)
    {
        cerr << e.what() << endl;
    }

    return 0;
}
