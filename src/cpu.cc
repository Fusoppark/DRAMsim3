#include "cpu.h"
#include <ctime>

namespace dramsim3 {

void RandomCPU::ClockTick() {
    // Create random CPU requests at full speed
    // this is useful to exploit the parallelism of a DRAM protocol
    // and is also immune to address mapping and scheduling policies
    //std::cout<<"before clocktick"<<std::endl;
    memory_system_.ClockTick();
	last_addr_ = AddressPair();
	if(clk_ == 0){
		dest = gen();
		dest1 = gen();
		Address temp1 = conf_->AddressMapping(dest);
		Address temp2 = conf_->AddressMapping(dest1);
		while(temp1.rank == temp2.rank){
			dest1 = gen();
			temp2 = conf_->AddressMapping(dest1);
		}
	}
	//std::cout<<clk_<<" cpu start"<<std::endl;
    if (get_next_) {
        if(is_copy_ && counter_ < 16){
            uint64_t sr = gen();
			Address src_ad = conf_->AddressMapping(sr);
			Address temp1 = conf_->AddressMapping(dest);
			Address temp2 = conf_->AddressMapping(dest1);
			if(temp1.rank == src_ad.rank){
				last_addr_ = AddressPair(sr, dest);
			}
			else{
				last_addr_ = AddressPair(sr, dest1);
			}
			last_addr_.is_copy = true;
			//std::cout<<"copy"<<std::endl;
            //last_addr_ = getRandomAddress();
            //last_addr_.is_copy = true;
			//if(clk_ == 0){
			//	dest = last_addr_.dest_addr;
			//}
			//Address temp1 = conf_->AddressMapping(dest);
			//Address temp2 = conf_->AddressMapping(last_addr_.src_addr);
			//while(temp1.rank != temp2.rank){
			//	last_addr_ = getRandomAddress();
			//	temp2 = conf_->AddressMapping(last_addr_.src_addr);
			//}
		}
        else{
			if(counter_ == 16){
				last_addr_ = dest;
			}
			else{last_addr_ = dest1;}
            last_addr_ = dest;
			last_addr_.is_copy = false;
			last_write_ = false;
            //last_write_ = (gen() % 3 == 0);
        }
    }
    //std::cout<<"willl"<<std::endl;
    get_next_ = memory_system_.WillAcceptTransaction(last_addr_, last_write_);
    if (get_next_ && counter_ < 18) {
		//std::cout<<last_addr_.src_addr<<" "<<last_addr_.dest_addr<<std::endl;
        //std::cout<<"yes"<<std::endl;
        //std::cout<< "Random : " << std::hex << last_addr_.src_addr << " " << std::hex << last_addr_.dest_addr << std::dec <<std::endl;
		memory_system_.AddTransaction(last_addr_, last_write_);
		counter_++;
    }
    clk_++;
    //std::cout<<clk_<<" cpu end"<<std::endl;
    return;
}

AddressPair RandomCPU::getRandomAddress(){
   // uint64_t hex_addr = gen();
	uint64_t hex_addr = dest;
	
    uint64_t channel_mask = conf_->ch_mask;
    channel_mask = channel_mask << conf_->ch_pos;
    channel_mask = channel_mask << conf_->shift_bits;

    uint64_t rank_mask = conf_->ra_mask;
    rank_mask = rank_mask << conf_->ra_pos;
    rank_mask = rank_mask << conf_->shift_bits;

    uint64_t uppermask = channel_mask | rank_mask;
    uint64_t mask = ~uppermask;

    uint64_t src_addr = (hex_addr & uppermask) + (gen() & mask);
    uint64_t dest_addr = (hex_addr & uppermask) + (gen() & mask);

    /*
    Address temp1 = conf_->AddressMapping(src_addr);
    Address temp2 = conf_->AddressMapping(dest_addr);
    std::cout << "SRC : " << std::hex << src_addr << " / DST : " << dest_addr << std::endl;
    std::cout << "SRC : " << temp1.rank << " " << temp1.bankgroup << " " << temp1.bank << " ";
    std::cout <<  "/ DST : " << temp2.rank << " " << temp2.bankgroup << " " << temp2.bank << std::endl;
     */

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
		addr_b_ = addr_a_ + stride_ * array_size_;
       // addr_b_ = gen();
        //addr_c_ = gen();
        offset_ = 0;
    }

    if (!inserted_a_ && counter < samples) {
        offset_ = (rand() % array_size_) * stride_;
		AddressPair copy_addr_ = AddressPair(addr_a_ + offset_, addr_b_ + stride_*counter);
		copy_addr_.is_copy = true;
		if(memory_system_.WillAcceptTransaction(copy_addr_, false)){
			memory_system_.AddTransaction(copy_addr_, false);
			inserted_a_ = true;
			counter += 1;
		}
    }
    /*if (!inserted_b_ &&
        memory_system_.WillAcceptTransaction(addr_b_ + offset_, false)) {
        memory_system_.AddTransaction(addr_b_ + offset_, false);
        inserted_b_ = true;
    }
    if (!inserted_c_ &&
        memory_system_.WillAcceptTransaction(addr_c_ + offset_, true)) {
        memory_system_.AddTransaction(addr_c_ + offset_, true);
        inserted_c_ = true;
    }*/
    // moving on to next element
    if (inserted_a_) {
        //offset_ += stride_;
        inserted_a_ = false;
        //inserted_b_ = false;
        //inserted_c_ = false;
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
