#ifndef __CPU_H
#define __CPU_H

#include <fstream>
#include <functional>
#include <random>
#include <string>
#include "memory_system.h"

namespace dramsim3 {

class CPU {
   public:
    CPU(const std::string& config_file, const std::string& output_dir)
        : memory_system_(
              config_file, output_dir,
              std::bind(&CPU::ReadCallBack, this, std::placeholders::_1),
              std::bind(&CPU::WriteCallBack, this, std::placeholders::_1)),
          clk_(0) 
          {
              conf_ = memory_system_.getConfig();
          }
    virtual void ClockTick() = 0;
    void ReadCallBack(AddressPair addr) { return; }
    void WriteCallBack(AddressPair addr) { return; }
    void PrintStats() { memory_system_.PrintStats(); }

   protected:
    MemorySystem memory_system_;
    uint64_t clk_;

    // RowClone added
    const Config* conf_;
};

class RandomCPU : public CPU {
   public:
    using CPU::CPU;
    void ClockTick() override;

    // Rowclone added
    AddressPair getRandomAddress(); // for two same rank address

   private:
    AddressPair last_addr_;
    bool last_write_ = false;
    std::mt19937_64 gen;
    bool get_next_ = true;
	uint64_t dest = 0;
	int counter_ = 0;
	uint64_t dest1 = 0;
    // RowClone added
    bool is_copy_ = true;
};

class StreamCPU : public CPU {
   public:
    using CPU::CPU;
    void ClockTick() override;

   private:
    AddressPair addr_a_, addr_b_, addr_c_, offset_ = 0;
    std::mt19937_64 gen;
    bool inserted_a_ = false;
    bool inserted_b_ = false;
    bool inserted_c_ = false;
    const uint64_t array_size_ = 100000;  // elements in array
    const int stride_ = 64;					// stride in bytes
	const uint64_t samples = 100;
	int counter = 0;
	uint64_t b_offset;
	uint64_t c_offset;
};

class TraceBasedCPU : public CPU {
   public:
    TraceBasedCPU(const std::string& config_file, const std::string& output_dir,
                  const std::string& trace_file);
    ~TraceBasedCPU() { trace_file_.close(); }
    void ClockTick() override;

   private:
    std::ifstream trace_file_;
    Transaction trans_;
    bool get_next_ = true;
};

}  // namespace dramsim3
#endif
