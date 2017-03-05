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
        virtual bool Upgr(int writer, int addr, int data) = 0;
        virtual bool RdX(int writer, int addr) = 0;
        virtual bool flush(int writer, int addr, uint8_t data[32]) = 0;
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
        BUS_UPGR,
        BUS_READX,
        BUS_FREE,
        FLUSH,
    };

    enum RetCode
    {
        RET_READ_DONE,
        RET_WRITE_DONE,
    };

     /* Five states for MOESI protocol. */
    enum Line_State
    {
        invalid,
        exclusive,
        modified,
        owned,
        shared,
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
    sc_in<int> 		  Port_BusReq;
  //  sc_in_rv<32*8>  Port_BusData;
    /* Bus requests ports. */
    sc_port<Bus_if> Port_Bus;

    /* Variables. */
    int cache_id;
    int probeRead;
    int probeWrite;

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
        cache_line() : state(invalid) {}
        Line_State state;
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
        int br;
        mem_addr_t mem_addr;
        int writer;
        int probeRead = 0;
        int probeWrite = 0;
        /* Continue while snooping is activated. */
        cout << "core " << cache_id << " begins to snoop" <<endl;

        while(true)
        {
                        /* Wait for work. */
            wait(Port_BusReq.value_changed_event());

            br = Port_BusReq.read();

            if (br == BUS_FREE) {
            	cout << "The bus is free!" << endl;
            	continue;
            }

                /* preparation for bus work*/
            mem_addr.addr = Port_BusAddr.read().to_int();
            writer = Port_BusWriter.read();

            cout << "-------------------------------------------"<< endl;
            cout << "BUS EXECUTES A REQUEST" << endl;
            cout << "cache_id:             " << cache_id <<endl;
            cout << "bus event changes at: " << sc_time_stamp() << endl;
            cout << "the writer core is :  " << writer <<endl;
            cout << "target address:       " << mem_addr.addr  <<endl;
            cout << "bus request:          " << br  << " ( 0: BUS_READ; 1: BUS_UPGR; 2: BUS_READX; 3: BUS_FREE)"<< endl;

            /* Possibilities. */

            // check if I am the requestor?
            if(writer != cache_id)
            {

                switch(br)
                {
                    case BUS_READ:
                     // nothing special needed to be done
                    	for ( int i=0; i< ASSOCIATIVITY;i++)
                      {
                          if (cache[mem_addr.set][i].tag == mem_addr.tag)
                          {
                              if (cache[mem_addr.set][i].state == modified || cache[mem_addr.set][i].state == exclusive || cache[mem_addr.set][i].state == owned)
                              {
                                cache[mem_addr.set][i].state == owned;
                                // flush the cache block to requesting core
                                Port_Bus->flush(cache_id, mem_addr.addr, cache[mem_addr.set][i].data);
                              //  probeRead++;
                              //  cout << "BUS READ:             " << probeRead  << endl;
                              }
                          	}
                        }
                      break;

                    case BUS_UPGR:
                      for ( int i=0; i< ASSOCIATIVITY;i++)
                      {
                          if (cache[mem_addr.set][i].tag == mem_addr.tag)
                          {
                              if (cache[mem_addr.set][i].state == invalid || cache[mem_addr.set][i].state == shared || cache[mem_addr.set][i].state == owned)
                              {
                                cache[mem_addr.set][i].state == invalid;
                                //  probeWrite++;
                                //  cout << "BUS WRITE:            " << probeWrite  << endl;
                                cout << "BUS UPGR: invalidate cache:" <<  i << "in the set of "<< mem_addr.set  <<  endl;
                              }
                          }
                      }
                      break;

                    // the difference of READX and WRITE is just the probe counter.

                    // NOTE: invalidate other copies
                    case BUS_READX:
                      for ( int i=0; i< ASSOCIATIVITY;i++)
                      {
                        if (cache[mem_addr.set][i].tag == mem_addr.tag)
                        {
                          cache[mem_addr.set][i].state == invalid;
                          cout << "BUS READX: invalidate cache:" <<  i << "in the set of "<< mem_addr.set  <<  endl;

                          if (cache[mem_addr.set][i].state == exclusive || cache[mem_addr.set][i].state == modified || cache[mem_addr.set][i].state == owned)
                          {
                             Port_Bus->flush(cache_id, mem_addr.addr, cache[mem_addr.set][i].data);
                            // probeRead++;
                            // cout << "BUS READX:            " << probeRead << "th time at:" << sc_time_stamp() << endl;
                          }
                        }
                      }
                      break;

                    case FLUSH:
                      break;

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
        Line_State ls = invalid;
        uint8_t data = 0;
        uint8_t target_line = 0;
        while (true)
        {
            wait(Port_Func.value_changed_event());  // this is fine since we use sc_buffer
            cout << "_____________________ debug line 1 ________________ " << endl;
            Function f = Port_Func.read();
            mem_addr.addr = Port_Addr.read();


            cout << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"<< endl;
            cout << "PROCESSOR PERFORMS A WRITE/READ FUNCTION"<< endl;
            cout << "cache_id:           " << cache_id << endl;
            cout << "targeting address:  " << mem_addr.addr <<endl;

            if(f == FUNC_WRITE) Write_Read = true;
            else if(f == FUNC_READ) Write_Read = false;

            hit = false;
            // First determine hit or miss

            for (int i = 0; i < ASSOCIATIVITY; i++) {
                if (cache[mem_addr.set][i].tag == mem_addr.tag && cache[mem_addr.set][i].state != invalid) {
                    hit = true;
                    target_line = i;

                    ls = cache[mem_addr.set][i].state;
                }
            }
            Hit_Point = hit;
            cout << "line state:         " << ls << "  [ 0: invalid; 1: exclusive, 2: modified, 3: owned, 4: shared]"  << endl;
            // FUNC_WRITE:
            // - If hit:    update the cache line
            //              invalidate other copies     BUS_WRITE
            //              update the LRU indices
            // - If miss:   determine LRU cache line
            //              set the valid bit
            //              invalidate other copies     BUS_READX
            //              write it back to RAM if line is valid (wait 100 cycles)
            //              replace data (wait 100 cycles)
            //              write new data
            //              update the LRU indices

            if (f == FUNC_WRITE) {
                cout << "function:          " << " FUNC_WRITE "  << endl;
                data = (uint8_t)Port_Data.read().to_int();

                switch (ls) {
                  case modified:
                    // no bus request is sent in modified case
                    // line state NOT changing
                    stats_writehit(cache_id);

                    cout << "line state:         "<< "Modified  ->  Modified  " << endl;
                    //Update the cache line
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;
                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);

                    Set_No = mem_addr.set;
                    Line_No = target_line;
                //    cout << "_____________________ debug line 1 ________________ " << endl;
                    //  do not write through to memory
                    // wait(100);
                    break;

                  case owned:
                   // NOTE: Send BUS_UPGR to bus to make other copies in shared state
                    stats_writehit(cache_id);
                    cout << "line state:         "<< "Owned  ->  Modified  "  << endl;
                    Port_Bus->Upgr(cache_id, mem_addr.addr, data);
                    //Update the cache line
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;
                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                    cache[mem_addr.set][target_line].state = modified;

                    Set_No = mem_addr.set;
                    Line_No = target_line;
                    // do not write through to memory
                    break;

                  case exclusive:
                    // no bus request is sent in exclusive case
                    stats_writehit(cache_id);
                    cout << "line state:         "<< "exclusive  ->  Modified  "  << endl;
                    //Update the cache line
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;
                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                    cache[mem_addr.set][target_line].state = modified;

                    Set_No = mem_addr.set;
                    Line_No = target_line;
                    //Update the cache line
                    break;

                  case shared:
                    stats_writehit(cache_id);
                    cout << "line state:         "<< "shared  ->  Modified  "  << endl;
                  // NOTE: send BUS_UPGR to bus to  invalidate other copies
                    Port_Bus->Upgr(cache_id, mem_addr.addr, data);
                    //Update the cache line
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;
                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                    cache[mem_addr.set][target_line].state = modified;

                    Set_No = mem_addr.set;
                    Line_No = target_line;
                    break;

                  case invalid:
                    stats_writemiss(cache_id);
                    cout << "line state:         "<< "invalid  ->  Modified  " << endl;
                  // NOTE: send BUS_READX to bus to invalidate copies
                    Port_Bus->RdX(cache_id,mem_addr.addr);
                    data = (uint8_t)(rand() % 255);
                    wait(100);
                    //Determine LRU line
                    target_line = get_LRU_line((uint8_t)mem_addr.set);
                    //Read new line from RAM (wait 100 cycles)
                    for (int i = 0; i < LINE_SIZE; i++) cache[mem_addr.set][target_line].data[i] = (uint8_t)(rand() % 255);
                    wait(100);
                    //Write new data in LRU line
                    cache[mem_addr.set][target_line].tag = mem_addr.tag;
                    cache[mem_addr.set][target_line].data[mem_addr.offset] = data;
                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                    cache[mem_addr.set][target_line].state = modified;

                    Line_No = target_line;
                    Set_No = mem_addr.set;
                    break;

                  default:
                    // NOTE: it should never happen
                    cout << "something wrong when the processor issues a PrWirte" << endl;
                    break;
                }
                Port_Done.write( RET_WRITE_DONE );
                cout << "_____________________ debug line 2 ________________ " << endl;
            }
            // FUNC_READ:
            // - If hit:    return the cache line
            //              update the LRU indices
            //              no bus request is sent
            // - If miss:   determine LRU cache line
            //              valid the cach line                BUS_READ
            //              write it back to RAM
            //              replace data with some random data (wait 100 cycles)
            //              return the cache line
            //              update the LRU indices

            if (f == FUNC_READ) {

                cout << "function:           " << "FUNC_READ" <<   endl;

                switch (ls) {
                  case modified:
                  // no bus request is sent in modified case

                  case owned:
                  // no bus request is sent in owned case

                  case exclusive:
                  // no bus request is sent in exclusive case

                  case shared:
                  // no bus request is sent in shared case
                    stats_readhit(cache_id);
                    cout << "line state:         "<< "line state unchanged  ->  Shared  " << endl;
                    Port_Data.write(cache[mem_addr.set][target_line].data[mem_addr.offset]);
                    wait(1);
                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);
                    Set_No = mem_addr.set;
                    Line_No = target_line;
                    break;/* value */

                  case invalid:
                  //NOTE: a BUS_READ is sent bus to fetch data
                    cout << "line state:         "<< "Invalid  ->  exclusive or shared "  << endl;
                    Port_Bus->Rd(cache_id, mem_addr.addr);
                    //Determine LRU line
                    target_line = get_LRU_line((uint8_t)mem_addr.set);
                    Line_No = target_line;
                    Set_No = mem_addr.set;
                    //cout<<"Miss Line Number: "<<unsigned(target_line)<<endl;

                    //Write back to RAM
                    //if (cache[mem_addr.set][target_line].dirty) wait(100);

                    if (Port_BusReq.read() == FLUSH){
                      // get flushed with the data from other core
                    //  cache[mem_addr.set][target_line].data = Port_BusData.read();
                      cache[mem_addr.set][target_line].state = shared;
                    }
                    else{
                      //Replace data with something from RAM
                      cache[mem_addr.set][target_line].tag = mem_addr.tag;
                      for (int i = 0; i < LINE_SIZE; i++)
                      {
                        cache[mem_addr.set][target_line].data[i] = (uint8_t)(rand() % 255);
                      }
                      cache[mem_addr.set][target_line].state = exclusive;
                    }

                    wait(100);

                    //Return the cache line
                    Port_Data.write(cache[mem_addr.set][target_line].data[mem_addr.offset]);

                    //Update LRU indices
                    update_LRU((uint8_t)mem_addr.set, target_line);

                    break;

                  default:
                    // NOTE: it should never happen
                    cout << "something wrong when the processor issues a PrRead" << endl;
                    break;
                }
                Port_Done.write( RET_READ_DONE );
                wait();
                Port_Data.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
            }
            cout << "_____________________ debug line 3 ________________ " << endl;
        }
          cout << "_____________________ debug line 4 ________________ " << endl;
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
                    cout << sc_time_stamp() << ": CPU: " <<  cpu_id << " sends write" << endl;

                    uint8_t data = rand() % 255;
                    Port_MemData.write(data);
                    wait();
                    Port_MemData.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
                }
                else
                {
                    cout << sc_time_stamp() << ": CPU: " <<  cpu_id << " sends read" << endl;
                }

                wait(Port_MemDone.value_changed_event());

                if (f == Cache::FUNC_READ)
                {
                    cout << sc_time_stamp() << ": CPU: " <<  cpu_id << " reads: " << Port_MemData.read() << endl;
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
    sc_out<int> Port_BusReq;
    sc_out<int> Port_BusWriter;
  //  sc_signal_rv<32*8>  Port_BusData;
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
        Port_BusReq.write(Cache::BUS_READ);

        /* Wait for everyone to recieve. */
        wait();

        Port_BusReq.write(Cache::BUS_FREE);
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        bus.unlock();

        return(true);
    };

    /* Write action to memory, need to know the writer, address and data. */
    virtual bool Upgr(int writer, int addr, int data){
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
        Port_BusReq.write(Cache::BUS_UPGR);

        /* Wait for everyone to recieve. */
        wait();

        /* Reset. */
      	Port_BusReq.write(Cache::BUS_FREE);
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
        Port_BusReq.write(Cache::BUS_READX);

        /* Wait for everyone to recieve. */
        wait();

        Port_BusReq.write(Cache::BUS_FREE);
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        bus.unlock();

        return(true);
    }

    virtual bool flush(int writer, int addr, uint8_t data[32]){
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
      //  Port_BusData.write(data);
        Port_BusReq.write(Cache::FLUSH);

        /* Wait for everyone to recieve. */
        wait();

        Port_BusReq.write(Cache::FLUSH);
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        bus.unlock();

        return(true);
    }

    /* Bus output. */
    void output(){
        /* Write output as specified in the assignment. */
        double avg = (double)waits / double(reads + writes);
        printf("\n 2. Main memory access rates\n");
        printf("    Bus had %ld reads and %ld writes.\n", reads, writes);
        printf("    A total of %ld accesses.\n", reads + writes);
        printf("\n 3. Average time for bus acquisition\n");
        printf("    There were %ld waits for the bus.\n", waits);
        printf("    Average waiting time per access: %f cycles.\n", avg);
    }
};
