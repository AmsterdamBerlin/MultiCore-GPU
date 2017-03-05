/*
// File: task_1.cpp
//              
// Framework to implement Task 1 of the Advances in Computer Architecture lab 
// session. This uses the ACA 2009 library to interface with tracefiles which
// will drive the read/write requests
//
// Author(s): Michiel W. van Tol, Mike Lankamp, Jony Zhang, 
//            Konstantinos Bousias
// Copyright (C) 2005-2009 by Computer Systems Architecture group, 
//                            University of Amsterdam
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
*/

#include "aca2009.h"
#include <systemc.h>
#include <iostream>
#include <iomanip>

using namespace std;

#define MEM_SIZE            32768
#define ASSOCIATIVITY       8
#define LINE_SIZE           32
#define NUM_SETS            ( ( MEM_SIZE / LINE_SIZE ) / ASSOCIATIVITY )

SC_MODULE(Memory) 
{

public:
    enum Function 
    {
        FUNC_READ,
        FUNC_WRITE
    };

    enum RetCode 
    {
        RET_READ_DONE,
        RET_WRITE_DONE,
    };

    sc_in<bool>     Port_CLK;
    sc_in<Function> Port_Func;
    sc_in<int>      Port_Addr;
    sc_out<RetCode> Port_Done;
    sc_inout_rv<8> Port_Data;

    SC_CTOR(Memory) 
    {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();

        cache = new cache_line_t[NUM_SETS][ASSOCIATIVITY];
    }

    ~Memory() 
    {
        delete[] cache;
    }

private:

    typedef union {
        struct {
            uint32_t offset: 5;
            uint32_t set: 7;
            uint32_t tag: 20;
        };
        uint32_t addr;
    } mem_addr_t;

    typedef struct {
        uint8_t age;
        uint32_t tag;
        uint8_t data[32];
    } cache_line_t;

    cache_line_t (*cache)[ASSOCIATIVITY];

    uint8_t get_LRU_line(uint8_t set) {

        for (int i = 0; i < ASSOCIATIVITY; i++){

            //If a line hasn't been used yet, use it
            if (cache[set][i].age == 0) return i;

        }

        uint8_t highest_age = 0;
        uint8_t LRU_line;

        for (int i = 0; i < ASSOCIATIVITY; i++){
            if (cache[set][i].age > highest_age) {
                highest_age = cache[set][i].age;
                LRU_line = i;
            }
        }
        return LRU_line;
    }

      void update_LRU(uint8_t set, uint8_t MRU_line) {

        //The line was already the most recently used, nothing to be done
        if (cache[set][MRU_line].age == 1) return;

        //Save the previous age
        uint8_t previous_age = cache[set][MRU_line].age;

        //Update MRU to age 1
        cache[set][MRU_line].age = 1;

        if (previous_age == 0) {
            for (int i = 0; i < ASSOCIATIVITY; i++) {
                //A cache line's age shouldn't be increased if:
                //- the line is empty
                //- the line is the one that we just used
                if (cache[set][i].age == 0 || i == MRU_line) continue;
                cache[set][i].age++;
            }
        }
        else {
            for (int i = 0; i < ASSOCIATIVITY; i++){

                //A cache line's age shouldn't be increased if:
                //- the line is empty
                //- the line is the one that we just used
                //- its age is already higher than the previous age

                if (cache[set][i].age == 0 || i == MRU_line || cache[set][i].age >= previous_age) continue;
                cache[set][i].age++;
            }
        }
    }

    void execute() {

        mem_addr_t mem_addr;
        bool hit;
        uint8_t data = 0;
        uint8_t target_line = 0;
        while (true)
        {
            wait(Port_Func.value_changed_event());  // this is fine since we use sc_buffer
            
            Function f = Port_Func.read();
            mem_addr.addr = Port_Addr.read();

            hit = false;
            // First determine hit or miss
            for (int i = 0; i < ASSOCIATIVITY; i++) {
                if (cache[mem_addr.set][i].tag == mem_addr.tag) {
                    hit = true;
                    target_line = i;
                }
            }

            // FUNC_WRITE:
            // - If hit:    update the cache line
            //              update the LRU indices
            // - If miss:   determine LRU cache line
            //              write it back to RAM (wait 100 cycles)
            //              replace data
            //              update the LRU indices

            if (f == FUNC_WRITE) {
                cout << sc_time_stamp() << ": MEM received write" << endl;
                data = (uint8_t)Port_Data.read().to_int();
                
                if (hit) {
                    stats_writehit(0);

                    //Update the cache line
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;

                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);

                    wait(1);
                }
                else {
                    stats_writemiss(0);
                    cout << "WRITE MISS" << endl;
                    //Determine LRU line
                    target_line = get_LRU_line((uint8_t)mem_addr.set);

                    //Write back to RAM (wait 100 cycles)
                    wait(100);

                    //Write new data in LRU line
                    cache[mem_addr.set][target_line].tag = mem_addr.tag;
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;

                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                }
                Port_Done.write( RET_WRITE_DONE );
            }

            // FUNC_READ:
            // - If hit:    return the cache line
            //              update the LRU indices
            // - If miss:   determine LRU cache line
            //              write it back to RAM
            //              replace data with some random data (wait 100 cycles)
            //              return the cache line
            //              update the LRU indices

            if (f == FUNC_READ) {
                cout << sc_time_stamp() << ": MEM received read" << endl;

                if (hit) {
                    stats_readhit(0);

                    //Return the cache line
                    Port_Data.write(cache[mem_addr.set][target_line].data[mem_addr.offset]);
                    wait(1);

                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                }
                else {
                    stats_readmiss(0);
                    cout << "READ MISS" << endl;

                    //Determine LRU line
                    target_line = get_LRU_line((uint8_t)mem_addr.set);

                    //Write back to RAM
                    wait(100);

                    //Replace data with something from RAM
                    cache[mem_addr.set][target_line].tag = mem_addr.tag;
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = (uint8_t)(rand() % 255);

                    //Return the cache line
                    Port_Data.write(cache[mem_addr.set][target_line].data[mem_addr.offset]);

                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                }

                Port_Done.write( RET_READ_DONE );
                Port_Data.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
            }
        }
    }
}; 

SC_MODULE(CPU) 
{

public:
    sc_in<bool>                 Port_CLK;
    sc_in<Memory::RetCode>      Port_MemDone;
    sc_out<Memory::Function>    Port_MemFunc;
    sc_out<int>                 Port_MemAddr;
    sc_inout_rv<8>              Port_MemData;

    SC_CTOR(CPU) 
    {
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();
    }

private:
    void execute() 
    {
        TraceFile::Entry    tr_data;
        Memory::Function    f;

        // Loop until end of tracefile
        while(!tracefile_ptr->eof())
        {
            // Get the next action for the processor in the trace
            if(!tracefile_ptr->next(0, tr_data))
            {
                cerr << "Error reading trace for CPU" << endl;
                break;
            }

            switch(tr_data.type)
            {
                case TraceFile::ENTRY_TYPE_READ:
                    f = Memory::FUNC_READ;
                    break;

                case TraceFile::ENTRY_TYPE_WRITE:
                    f = Memory::FUNC_WRITE;
                    break;

                case TraceFile::ENTRY_TYPE_NOP:
                    break;

                default:
                    cerr << "Error, got invalid data from Trace" << endl;
                    exit(0);
            }

            if(tr_data.type != TraceFile::ENTRY_TYPE_NOP)
            {
                Port_MemAddr.write(tr_data.addr);
                Port_MemFunc.write(f);

                if (f == Memory::FUNC_WRITE) 
                {
                    cout << sc_time_stamp() << ": CPU sends write" << endl;

                    uint8_t data = rand() % 255;
                    Port_MemData.write(data);
                    wait();
                    Port_MemData.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
                }
                else
                {
                    cout << sc_time_stamp() << ": CPU sends read" << endl;
                }

                wait(Port_MemDone.value_changed_event());

                if (f == Memory::FUNC_READ)
                {
                    cout << sc_time_stamp() << ": CPU reads: " << Port_MemData.read() << endl;
                }
            }
            else
            {
                cout << sc_time_stamp() << ": CPU executes NOP" << endl;
            }
            // Advance one cycle in simulated time            
            wait();
        }
        
        // Finished the Tracefile, now stop the simulation
        sc_stop();
    }
};


int sc_main(int argc, char* argv[])
{
    try
    {
        // Get the tracefile argument and create Tracefile object
        // This function sets tracefile_ptr and num_cpus
        init_tracefile(&argc, &argv);

        // Initialize statistics counters
        stats_init();

        // Instantiate Modules
        Memory mem("main_memory");
        CPU    cpu("cpu");

        // Signals
        sc_buffer<Memory::Function> sigMemFunc;
        sc_buffer<Memory::RetCode>  sigMemDone;
        sc_signal<int>              sigMemAddr;
        sc_signal_rv<8>             sigMemData;

        // The clock that will drive the CPU and Memory
        sc_clock clk;

        // Connecting module ports with signals
        mem.Port_Func(sigMemFunc);
        mem.Port_Addr(sigMemAddr);
        mem.Port_Data(sigMemData);
        mem.Port_Done(sigMemDone);

        cpu.Port_MemFunc(sigMemFunc);
        cpu.Port_MemAddr(sigMemAddr);
        cpu.Port_MemData(sigMemData);
        cpu.Port_MemDone(sigMemDone);

        mem.Port_CLK(clk);
        cpu.Port_CLK(clk);

        cout << "Running (press CTRL+C to interrupt)... " << endl;


        // Start Simulation
        sc_start();
        
        // Print statistics after simulation finished
        stats_print();
    }

    catch (exception& e)
    {
        cerr << e.what() << endl;
    }
    
    return 0;
}
