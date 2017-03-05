/*
// File: task_1_waveform.cpp
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



/* Bus interface, modified version from assignment. */
class Bus_if : public virtual sc_interface 
{
    public:
        virtual bool Rd(int writer, int addr) = 0;
        virtual bool Wr(int writer, int addr, int data) = 0;
        virtual bool RdX(int writer, int addr) = 0;
};




SC_MODULE(Cache) 
{

 //sc_inout< sc_uint<8> > bus;

public:
    enum Function 
    {
        FUNC_READ,
        FUNC_WRITE,
    };

    enum BusRequest{
        BUS_READ,
        BUS_WRITE,
        BUS_READX,
        BUS_FREE,
    };

    enum RetCode 
    {
        RET_READ_DONE,
        RET_WRITE_DONE,
    };

     /* Possible line states depending on the cache coherence protocol. */
    enum Line_State 
    {
        INVALID,
        VALID
    };

    sc_in<bool>     Port_CLK;
    sc_in<Function> Port_Func;
    sc_in<int>      Port_Addr;
    sc_out<RetCode> Port_Done;
    sc_inout_rv<8>  Port_Data;
    sc_out<uint8_t> Set_No;
    sc_out<uint8_t> Line_No;
    sc_out<bool>    Hit_Point;
    sc_out<bool>    Write_Read;


    /* Bus snooping ports. */
    sc_in_rv<32>    Port_BusAddr;
    sc_in<int>      Port_BusWriter;
    sc_in<BusRequest> Port_BusValid;

    /* Bus requests ports. */
    sc_port<Bus_if> Port_Bus;

    /* Variables. */
    int cache_id;


    SC_CTOR(Cache) 
    {
        cache_id = 0;
        SC_THREAD(bus); //*********************** thread for bus
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();

        cache = new cache_line_t[NUM_SETS][ASSOCIATIVITY];
    }

    ~Cache() 
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

    typedef struct cache_line{
        cache_line() : valid(false) {}
        bool valid;
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
    /* Thread that handles the bus. */
    void bus() 
    {
        BusRequest br;
        mem_addr_t mem_addr;
        int writer;
        int probeRead = 0;
        int probeWrite = 0;
        /* Continue while snooping is activated. */
       // cout << "core " << cache_id << " begins to snoop" <<endl;

        while(true)
        {
                        /* Wait for work. */
            wait(Port_BusValid.value_changed_event());
            
                /* preparation for bus work*/
            mem_addr.addr = Port_BusAddr.read().to_int();
            br = Port_BusValid.read();
            writer = Port_BusWriter.read();

         //   cout << "-------------------------------------------"<< endl;
        //    cout << "cache_id:             " << cache_id <<endl;
       //     cout << "bus event changes at: " << sc_time_stamp() << endl;
       //     cout << "the writer core is :  " << writer <<endl;
        //    cout << "target address:       " << mem_addr.addr  <<endl;
         //   cout << "bus request:          " << br  << " ( 0: BUS_READ; 1: BUS_WRITE; 2: BUS_READX )"<< endl;
            
            /* Possibilities. */
            // check if I am the requestor?
            if(writer != cache_id)
            { 

                switch(br)
                {
            // your code of what to do while snooping the bus
            // keep in mind that a certain cache should distinguish between bus requests made by itself and requests made by other caches.
            // count and report the total number of read/write operations on the bus, in which the desired address (by other caches) is found in the snooping cache (probe read hits and probe write hits).
                    
                    case BUS_READ:
                    {// nothing special needed to be done

                        for ( int i=0; i< ASSOCIATIVITY;i++)
                        {
                            if (cache[mem_addr.set][i].tag == mem_addr.tag)
                            {   
                                
                                if( cache[mem_addr.set][i].valid == true){
                        //        cout << "BUS WRITE: invalidate cache:" <<  i << "in the set of "<< mem_addr.set  <<  endl;
                                    probeRead++;
                                    
                                }
                            }           
                        }
                     //   cout << "BUS READ:             " << probeRead  << endl;
                        break;  
                    }   

                    case BUS_WRITE:
                    {
                        for ( int i=0; i< ASSOCIATIVITY;i++)
                        {
                            if (cache[mem_addr.set][i].tag == mem_addr.tag)
                            {   
                                
                                cache[mem_addr.set][i].valid = false;
                          //      cout << "BUS WRITE: invalidate cache:" <<  i << "in the set of "<< mem_addr.set  <<  endl;
                                probeWrite++;
                            }           
                        }
                        
                     //   cout << "BUS WRITE:            " << probeWrite  << endl;
                        break;  
                    }
            // the diference of READX and WRITE is just the probe counter.
                    case BUS_READX:
                    {
                        for ( int i=0; i< ASSOCIATIVITY;i++)
                        {
                            if (cache[mem_addr.set][i].tag == mem_addr.tag)
                            {   
                                
                                cache[mem_addr.set][i].valid = false;
                        //        cout << "BUS WRITE: invalidate cache:" <<  i << "in the set of "<< mem_addr.set  <<  endl;
                                probeRead++;
                            }   
                        }
                        
                   //     cout << "BUS READX:            " << probeRead << "th time at:" << sc_time_stamp() << endl;
                        break; 
                    }
                    case BUS_FREE:
                        break;

                    default:
                        cout << "cannot check the bus request! bus function is wrong!! checkout the bus!!"<< "at: " << sc_time_stamp() << endl;
                        break;
                }
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


          //  cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"<< endl;
         //   cout << "cache_id:           " << cache_id << endl;
          //  cout << "targeting address:  " << mem_addr.addr <<endl;

            if(f == FUNC_WRITE) Write_Read = true;
            else if(f == FUNC_READ) Write_Read = false;

            hit = false;
            // First determine hit or miss
            for (int i = 0; i < ASSOCIATIVITY; i++) {
                if (cache[mem_addr.set][i].tag == mem_addr.tag && cache[mem_addr.set][i].valid) {
                    hit = true;
                    target_line = i;
                }
            }
            Hit_Point = hit;

            // FUNC_WRITE:
            // - If hit:    update the cache line
            //              set the dirty bit
            //              update the LRU indices
            // - If miss:   determine LRU cache line
            //              write it back to RAM if line is dirty (wait 100 cycles)
            //              replace data (wait 100 cycles)
            //              write new data
            //              set the dirty bit
            //              update the LRU indices

            if (f == FUNC_WRITE) {
                       
             //   cout << "functinon:           " << " FUNC_WRITE " << "cache_id:           " << cache_id << endl;
                data = (uint8_t)Port_Data.read().to_int();
                
                if (hit) {

                    stats_writehit(cache_id);
                    // when it is write hit on a valid line, issue a BUS WRITE to invalidate other copies.
                    if (cache[mem_addr.set][target_line].valid == true)
                    {
                               
                 //       cout << "FUNC_WRITE          "<< "Hit:         Valid " <<  "cache_id:           " << cache_id << endl;
                        Port_Bus->Wr(cache_id,mem_addr.addr, data);
                    } 
                    else
                    {
                //        cout << "FUNC_WRITE:         "<< "Hit:        Invalid " <<  "cache_id:           " << cache_id << endl; 
                        Port_Bus->RdX(cache_id,mem_addr.addr);  

                        // fetch the data from memory
                        data = (uint8_t)(rand() % 255);
                        wait(100);
                    }

                        

                    //Update the cache line
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;

                    cache[mem_addr.set][target_line].valid = true;
                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);

                    Set_No = mem_addr.set;
                    Line_No = target_line;

                    

                    // write through to memory
                    wait(100); 

                }
                else {
                    // when it is a write miss, issue a BUS RDX to read data from memory  ???? 

                    stats_writemiss(cache_id);

                //    cout << "FUNC_WRITE          "<< "Miss        " <<  "  cache_id:           " << cache_id << endl;
                    Port_Bus->RdX(cache_id,mem_addr.addr);
                    data = (uint8_t)(rand() % 255);
                    wait(100);
                   

                    //Determine LRU line
                    target_line = get_LRU_line((uint8_t)mem_addr.set);
                    Line_No = target_line;
                    Set_No = mem_addr.set;


                    //Read new line from RAM (wait 100 cycles)
                    for (int i = 0; i < LINE_SIZE; i++) cache[mem_addr.set][target_line].data[i] = (uint8_t)(rand() % 255);
                    wait(100);

                    //Write new data in LRU line
                    cache[mem_addr.set][target_line].tag = mem_addr.tag;
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;
                    cache[mem_addr.set][target_line].valid = true;

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

              //  cout << "function:            " << " FUNC_READ" <<  "  cache_id:           " << cache_id << endl;
                        
                if (hit) {   
                    // when it is a valid line, delivery a hit
                    stats_readhit(cache_id);

                    if(cache[mem_addr.set][target_line].valid == true)
                    {
                   //     cout << "FUNC_READ         "<< "Hit:         Valid " <<  "  cache_id:           " << cache_id << endl;
                    }
                    else
                    {
                    //    cout << "FUNC_READ         "<< "Hit:        Invalid " << "  cache_id:           " << cache_id <<  endl;
                        Port_Bus->Rd(cache_id,mem_addr.addr);
                        wait(100);
                    }
                       

                        //Return the cache line
                    Port_Data.write(cache[mem_addr.set][target_line].data[mem_addr.offset]);
                    wait(1);

                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                    Set_No = mem_addr.set;
                    Line_No = target_line;

     
            
                }
                else {    
                    // when it is a read MISS, issue BUS READ for memory`s reply

                    stats_readmiss(cache_id);

                    Port_Bus->Rd(cache_id,mem_addr.addr);

                    
                   // cout << "FUNC_READ          "<< "Miss" <<   "  cache_id:           " << cache_id << endl;
                    //Determine LRU line
                    target_line = get_LRU_line((uint8_t)mem_addr.set);
                    Line_No = target_line;
                    Set_No = mem_addr.set;
                    //cout<<"Miss Line Number: "<<unsigned(target_line)<<endl;

                    //Write back to RAM

                    //Replace data with something from RAM
                    cache[mem_addr.set][target_line].tag = mem_addr.tag;
                    for (int i = 0; i < LINE_SIZE; i++) cache[mem_addr.set][target_line].data[i] = (uint8_t)(rand() % 255);

                    cache[mem_addr.set][target_line].valid = true;
                    wait(100);

                    //Return the cache line
                    Port_Data.write(cache[mem_addr.set][target_line].data[mem_addr.offset]);

                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                }

                Port_Done.write( RET_READ_DONE );
                wait();
                Port_Data.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
            }
        }
    }
}; 

SC_MODULE(CPU) 
{

public:
    sc_in<bool>                 Port_CLK;
    sc_in<Cache::RetCode>      Port_MemDone;
    sc_out<Cache::Function>    Port_MemFunc;
    sc_out<int>                 Port_MemAddr;
    sc_inout_rv<8>              Port_MemData;

    int cpu_id;

    SC_CTOR(CPU) 
    {
        cpu_id = 0;
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();
    }

private:
    void execute() 
    {
        TraceFile::Entry    tr_data;
        Cache::Function    f;

        // Loop until end of tracefile
        while(!tracefile_ptr->eof())
        {
            // Get the next action for the processor in the trace
            if(!tracefile_ptr->next(cpu_id, tr_data))
            {
                cerr << "Error reading trace for CPU: " <<  cpu_id  << endl;
                break;
            }

            switch(tr_data.type)
            {
                case TraceFile::ENTRY_TYPE_READ:
                    f = Cache::FUNC_READ;
                    break;

                case TraceFile::ENTRY_TYPE_WRITE:
                    f = Cache::FUNC_WRITE;
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

                if (f == Cache::FUNC_WRITE) 
                {
                    //cout << sc_time_stamp() << ": CPU: " <<  cpu_id << " sends write" << endl;

                    uint8_t data = rand() % 255;
                    Port_MemData.write(data);
                    wait();
                    Port_MemData.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
                }
                else
                {
                  //  cout << sc_time_stamp() << ": CPU: " <<  cpu_id << " sends read" << endl;
                }

                wait(Port_MemDone.value_changed_event());

                if (f == Cache::FUNC_READ)
                {
                 //   cout << sc_time_stamp() << ": CPU: " <<  cpu_id << " reads: " << Port_MemData.read() << endl;
                }
            }
            else
            {
               // cout << sc_time_stamp() << ": CPU executes NOP" << endl;
            }
            // Advance one cycle in simulated time            
            wait();
        }
        
        // Finished the Tracefile, now stop the simulation
        sc_stop();
    }
};


/* Bus class, provides a way to share one memory in multiple CPU + Caches. */
class Bus : public Bus_if, public sc_module {
public:

    /* Ports andkkk  vb Signals. */
    sc_in<bool> Port_CLK;
    sc_out<Cache::BusRequest> Port_BusValid;
    sc_out<int> Port_BusWriter;

    sc_signal_rv<32> Port_BusAddr;

    /* Bus mutex. */
    sc_mutex bus;

    /* Variables. */
    long waits;
    long reads;
    long writes;

public:
    /* Constructor. */
    SC_CTOR(Bus) {
        /* Handle Port_CLK to simulate delay */
        sensitive << Port_CLK.pos();

        // Initialize some bus properties
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");

        /* Update variables. */
        waits = 0;
        reads = 0;
        writes = 0;
    }

    /* Perform a read access to memory addr for CPU #writer. */
    virtual bool Rd(int writer, int addr){
        /* Try to get exclusive lock on bus. */
        while(bus.trylock() == -1){
            /* Wait when bus is in contention. */
            waits++;
            wait();
        }

        /* Update number of bus accesses. */
        reads++;

        /* Set lines. */
        Port_BusAddr.write(addr);
        Port_BusWriter.write(writer);
        Port_BusValid.write(Cache::BUS_READ);

        /* Wait for everyone to recieve. */
        wait();

     //   Port_BusValid.write(Cache::BUS_FREE);
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        bus.unlock();

        return(true);
    };

    /* Write action to memory, need to know the writer, address and data. */
    virtual bool Wr(int writer, int addr, int data){
        /* Try to get exclusive lock on the bus. */
        while(bus.trylock() == -1){
            waits++;
            wait();
        }

        /* Update number of accesses. */
        writes++;

        /* Set. */
        Port_BusAddr.write(addr);
        Port_BusWriter.write(writer);
        Port_BusValid.write(Cache::BUS_WRITE);

        /* Wait for everyone to recieve. */
        wait();

        /* Reset. */
     //   Port_BusValid.write(Cache::BUS_FREE);
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        bus.unlock();

        return(true);
    }

    virtual bool RdX(int writer, int addr){
        /* Try to get exclusive lock on the bus. */
        while(bus.trylock() == -1){
            waits++;
            wait();
        }

        /* Update number of accesses. */
        reads++;

        /* Set lines. */
        Port_BusAddr.write(addr);
        Port_BusWriter.write(writer);
        Port_BusValid.write(Cache::BUS_READX);

        /* Wait for everyone to recieve. */
        wait();

      //  Port_BusValid.write(Cache::BUS_FREE);
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        bus.unlock();

        return(true);
    }

    /* Bus output. */
    void output(){
        /* Write output as specified in the assignment. */
        double avg = (double)waits / double(reads + writes);
        printf("\n 2. Main memory access rates\n");
        printf("    Bus had %d reads and %d writes.\n", reads, writes);
        printf("    A total of %d accesses.\n", reads + writes);
        printf("\n 3. Average time for bus acquisition\n");
        printf("    There were %d waits for the bus.\n", waits);
        printf("    Average waiting time per access: %f cycles.\n", avg);
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
        sc_signal<Cache::BusRequest> sigBusValid;

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
        bus.Port_BusValid(sigBusValid);

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
            cache[i]->Port_BusValid(sigBusValid);
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
        //sc_trace_file *wf = sc_create_vcd_trace_file("final_cache");
        // Dump the desired signals
        //sc_trace(wf, clk, "clock");
        //sc_trace(wf, sigMemAddr, "address");
        //sc_trace(wf, sigSet, "set_number");
        //sc_trace(wf, sigLine, "line_number");
        //sc_trace(wf, sigHit, "Hit/Miss");
        //sc_trace(wf, sigWR, "Write/Read");    

        cout << "Running (press CTRL+C to interrupt)... " << endl;

        // Start Simulation
        sc_start();
        
        // Print statistics after simulation finished
        stats_print();
        bus.output();
        //sc_close_vcd_trace_file(wf);
        return 0;
    }

    catch (exception& e)
    {
        cerr << e.what() << endl;
    }
    
    return 0;
}