#include "cpu.h"
#include <ctime>

namespace dramsim3 {

void RandomCPU::ClockTick() {
    // Create random CPU requests at full speed
    // this is useful to exploit the parallelism of a DRAM protocol
    // and is also immune to address mapping and scheduling policies
    std::cout<<"before clocktick"<<std::endl;
    memory_system_.ClockTick();
    std::cout<<clk_<<" cpu start"<<std::endl;
    if (get_next_) {
        if(is_copy_){
            std::cout<<"copy"<<std::endl;
            last_addr_ = getRandomAddress();
            last_addr_.is_copy = true;
        }
        else{
            last_addr_ = gen();
            last_write_ = (gen() % 3 == 0);
        }
    }
    std::cout<<"willl"<<std::endl;
    get_next_ = memory_system_.WillAcceptTransaction(last_addr_, last_write_);
    if (get_next_) {
        std::cout<<"yes"<<std::endl;
        std::cout<< "Random : " << std::hex << last_addr_.src_addr << " " << std::dec << last_addr_.dest_addr << std::dec <<std::endl;
        memory_system_.AddTransaction(last_addr_, last_write_);
    }
    clk_++;
    std::cout<<clk_<<" cpu end"<<std::endl;
    return;
}

AddressPair RandomCPU::getRandomAddress(){
    uint64_t hex_addr = gen();
    uint64_t mask = pow(2, conf_->ra_pos) - 1;
    uint64_t uppermask = 0xffffffffffffffff;
    uppermask = uppermask >> conf_->ra_pos;
    uppermask = uppermask << conf_->ra_pos;

    uint64_t src_addr = (hex_addr & uppermask) + (gen() & mask);
    uint64_t dest_addr = (hex_addr & uppermask) + (gen() & mask);

    return AddressPair(src_addr, dest_addr);
}

void StreamCPU::ClockTick() {
    // stream-add, read 2 arrays, add them up to the third array
    // this is a very simple approximate but should be able to produce
    // enough buffer hits

    // moving on to next set of arrays
    memory_system_.ClockTick();
    if (offset_ >= array_size_ || clk_ == 0) {
        addr_a_ = gen();
        addr_b_ = gen();
        addr_c_ = gen();
        offset_ = 0;
    }

    if (!inserted_a_ &&
        memory_system_.WillAcceptTransaction(addr_a_ + offset_, false)) {
        memory_system_.AddTransaction(addr_a_ + offset_, false);
        inserted_a_ = true;
    }
    if (!inserted_b_ &&
        memory_system_.WillAcceptTransaction(addr_b_ + offset_, false)) {
        memory_system_.AddTransaction(addr_b_ + offset_, false);
        inserted_b_ = true;
    }
    if (!inserted_c_ &&
        memory_system_.WillAcceptTransaction(addr_c_ + offset_, true)) {
        memory_system_.AddTransaction(addr_c_ + offset_, true);
        inserted_c_ = true;
    }
    // moving on to next element
    if (inserted_a_ && inserted_b_ && inserted_c_) {
        offset_ += stride_;
        inserted_a_ = false;
        inserted_b_ = false;
        inserted_c_ = false;
    }
    clk_++;
    return;
}

TraceBasedCPU::TraceBasedCPU(const std::string& config_file,
                             const std::string& output_dir,
                             const std::string& trace_file)
    : CPU(config_file, output_dir) {
    trace_file_.open(trace_file);
    if (trace_file_.fail()) {
        std::cerr << "Trace file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
}

void TraceBasedCPU::ClockTick() {
    memory_system_.ClockTick();
    if (!trace_file_.eof()) {
        if (get_next_) {
            get_next_ = false;
            trace_file_ >> trans_;
        }
        if (trans_.added_cycle <= clk_) {
            get_next_ = memory_system_.WillAcceptTransaction(trans_.addr,
                                                             trans_.is_write);
            if (get_next_) {
                memory_system_.AddTransaction(trans_.addr, trans_.is_write);
            }
        }
    }
    clk_++;
    return;
}

}  // namespace dramsim3
