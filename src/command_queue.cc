#include "command_queue.h"

namespace dramsim3 {

CommandQueue::CommandQueue(int channel_id, const Config& config,
                           const ChannelState& channel_state,
                           SimpleStats& simple_stats)
    : rank_q_empty(config.ranks, true),
      config_(config),
      channel_state_(channel_state),
      simple_stats_(simple_stats),
      is_in_ref_(false),
      queue_size_(static_cast<size_t>(config_.cmd_queue_size)),
      queue_idx_(0),
      clk_(0) {
    if (config_.queue_structure == "PER_BANK") {
        queue_structure_ = QueueStructure::PER_BANK;
        num_queues_ = config_.banks * config_.ranks;
    } else if (config_.queue_structure == "PER_RANK") {
        queue_structure_ = QueueStructure::PER_RANK;
        num_queues_ = config_.ranks;
    } else {
        std::cerr << "Unsupportted queueing structure "
                  << config_.queue_structure << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    queues_.reserve(num_queues_);
    for (int i = 0; i < num_queues_; i++) {
        auto cmd_queue = std::vector<Command>();
        cmd_queue.reserve(config_.cmd_queue_size);
        queues_.push_back(cmd_queue);
    }
}

Command CommandQueue::GetCommandToIssue() {
    for (int i = 0; i < num_queues_; i++) {
        auto& queue = GetNextQueue();

        // if we're refresing, skip the command queues that are involved
        if (is_in_ref_) {
            if (ref_q_indices_.find(queue_idx_) != ref_q_indices_.end()) {
                continue;
            }
        }
        
        auto cmd = GetFirstReadyInQueue(queue);

        // --------------------------------------------------------------------------
        // RowClone added
        // If READCOPY is going to be issued, force to find pair WRITECOPY in next for loop

        if(cmd.cmd_type == CommandType::READCOPY){

            is_in_copy_ = true;
            copy_address_pair_ = cmd.hex_addr;
            auto addr = config_.AddressMapping(cmd.hex_addr.dest_addr);
            queue_idx_ = GetQueueIndex(addr.rank, addr.bankgroup, addr.bank) - 1;
        }

        // ---------------------------------------------------------------------------

        std::cout << std::hex << "Issued : " << cmd.hex_addr.src_addr << " " << cmd.hex_addr.dest_addr << std::dec;
        if(cmd.cmd_type == CommandType::READCOPY){std::cout << " " << "READCOPY";}
        if(cmd.cmd_type == CommandType::WRITECOPY){std::cout << " " << "WRITECOPY";}
        std::cout << std::endl;

        if (cmd.IsValid()) {
            if (cmd.IsReadWrite()) {
                EraseRWCommand(cmd);
            }
            //std::cout<<clk_<<" getcommand"<<std::endl;
            std::cout<<"read: "<<cmd.IsRead()<<" write: "<<cmd.IsWrite()<<" readcopy: "<<cmd.IsReadCopy()<< \
            " writecopy: "<<cmd.IsWriteCopy()<<" refresh: "<<cmd.IsRefresh()<<std::endl;
            return cmd;
        }
    }
    return Command();
}

// RowClone Added
void CommandQueue::InCopyFlagDown(){
    is_in_copy_ = false;
}


Command CommandQueue::FinishRefresh() {
    // we can do something fancy here like clearing the R/Ws
    // that already had ACT on the way but by doing that we
    // significantly pushes back the timing for a refresh
    // so we simply implement an ASAP approach
    auto ref = channel_state_.PendingRefCommand();
    if (!is_in_ref_) {
        GetRefQIndices(ref);
        is_in_ref_ = true;
    }

    // either precharge or refresh
    auto cmd = channel_state_.GetReadyCommand(ref, clk_);

    if (cmd.IsRefresh()) {
        ref_q_indices_.clear();
        is_in_ref_ = false;
    }
    return cmd;
}

bool CommandQueue::ArbitratePrecharge(const CMDIterator& cmd_it,
                                      const CMDQueue& queue) const {
    auto cmd = *cmd_it;

    for (auto prev_itr = queue.begin(); prev_itr != cmd_it; prev_itr++) {
        if (prev_itr->Rank() == cmd.Rank() &&
            prev_itr->Bankgroup() == cmd.Bankgroup() &&
            prev_itr->Bank() == cmd.Bank()) {
            return false;
        }
    }

    bool pending_row_hits_exist = false;
    int open_row =
        channel_state_.OpenRow(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    for (auto pending_itr = cmd_it; pending_itr != queue.end(); pending_itr++) {
        if (pending_itr->Row() == open_row &&
            pending_itr->Bank() == cmd.Bank() &&
            pending_itr->Bankgroup() == cmd.Bankgroup() &&
            pending_itr->Rank() == cmd.Rank()) {
            pending_row_hits_exist = true;
            break;
        }
    }

    bool rowhit_limit_reached =
        channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(), cmd.Bank()) >=
        4;
    if (!pending_row_hits_exist || rowhit_limit_reached) {
        simple_stats_.Increment("num_ondemand_pres");
        return true;
    }
    return false;
}

bool CommandQueue::WillAcceptCommand(int rank, int bankgroup, int bank, bool additional) const {
    int q_idx = GetQueueIndex(rank, bankgroup, bank);
    if(additional){
        return queues_[q_idx].size() + 1 < queue_size_;
    }
    return queues_[q_idx].size() < queue_size_;
}

bool CommandQueue::QueueEmpty() const {
    for (const auto q : queues_) {
        if (!q.empty()) {
            return false;
        }
    }
    return true;
}


bool CommandQueue::AddCommand(Command cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    if (queue.size() < queue_size_) {
        queue.push_back(cmd);
        rank_q_empty[cmd.Rank()] = false;
        return true;
    } else {
        return false;
    }
}

CMDQueue& CommandQueue::GetNextQueue() {
    queue_idx_++;
    if (queue_idx_ == num_queues_) {
        queue_idx_ = 0;
    }
    return queues_[queue_idx_];
}

void CommandQueue::GetRefQIndices(const Command& ref) {
    if (ref.cmd_type == CommandType::REFRESH) {
        if (queue_structure_ == QueueStructure::PER_BANK) {
            for (int i = 0; i < num_queues_; i++) {
                if (i / config_.banks == ref.Rank()) {
                    ref_q_indices_.insert(i);
                }
            }
        } else {
            ref_q_indices_.insert(ref.Rank());
        }
    } else {  // refb
        int idx = GetQueueIndex(ref.Rank(), ref.Bankgroup(), ref.Bank());
        ref_q_indices_.insert(idx);
    }
    return;
}

int CommandQueue::GetQueueIndex(int rank, int bankgroup, int bank) const {
    if (queue_structure_ == QueueStructure::PER_RANK) {
        return rank;
    } else {
        return rank * config_.banks + bankgroup * config_.banks_per_group +
               bank;
    }
}

CMDQueue& CommandQueue::GetQueue(int rank, int bankgroup, int bank) {
    int index = GetQueueIndex(rank, bankgroup, bank);
    return queues_[index];
}

Command CommandQueue::GetFirstReadyInQueue(CMDQueue& queue) const {
    //std::cout<<clk_<<" getfirtstreadyinqueue"<<std::endl;

    // ---------------------------------------------------------------
    // RowClone added
    if(is_in_copy_){
        unsigned cmd_idx = 0;
        while((queue[cmd_idx].cmd_type != CommandType::WRITECOPY) && (queue[cmd_idx].hex_addr != copy_address_pair_) && (cmd_idx < queue.size())){
            cmd_idx++;
        }

        // move copy cmd to the front
        if(cmd_idx < queue.size()){
            Command copy_temp = queue[cmd_idx];
            for(unsigned i=cmd_idx; i > 0; i++){
                queue[i] = queue[i-1];
            }
            queue[0] = copy_temp;
        }

        // only checks if cmd is valid
        auto cmd_it = queue.begin();
        Command cmd = channel_state_.GetReadyCommand(*cmd_it, clk_);
        if (!cmd.IsValid()) {
            return Command();
        }
        return cmd;

    }

    // -------------------------------------------------------------------


    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        Command cmd = channel_state_.GetReadyCommand(*cmd_it, clk_);
        if (!cmd.IsValid()) {
            continue;
        }
        if (cmd.cmd_type == CommandType::PRECHARGE) {
            if (!ArbitratePrecharge(cmd_it, queue)) {
                continue;
            }
        } else if (cmd.IsWrite()) {
            if (HasRWDependency(cmd_it, queue)) {
                continue;
            }
        } else if (cmd.IsWriteCopy()){
            continue;
        } else if (cmd.IsReadCopy()){
            // check bank same or not
            //std::cout<<clk_<<" isreadcopy"<<std::endl;
            auto src_address = cmd.addr;
            auto dest_address = config_.AddressMapping(cmd.hex_addr.dest_addr);

            if(src_address.bankgroup != dest_address.bankgroup || \
                src_address.bank != dest_address.bank){
                // different bank
                cmd.isFPM = false;
                // check whether dest bank can start waiting for WRITE_COPY
                if(!channel_state_.CanStartWait(Command(CommandType::WRITECOPY, \
                    dest_address, cmd.hex_addr), clk_)){
                    // dest bank can not start waiting for WRITE_COPY
                    //std::cout<<"no writecopy"<<std::endl;
                    continue;
                }
            }
            else{
                cmd.isFPM = true;
            }
            //std::cout<<clk_<<" readcopy issue"<<std::endl;
        }
        return cmd;
    }
    return Command();
}

void CommandQueue::EraseRWCommand(const Command& cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        if (cmd.hex_addr == cmd_it->hex_addr && cmd.cmd_type == cmd_it->cmd_type) {
            queue.erase(cmd_it);
            return;
        }
    }
    std::cerr << "cannot find cmd!" << std::endl;
    exit(1);
}

int CommandQueue::QueueUsage() const {
    int usage = 0;
    for (auto i = queues_.begin(); i != queues_.end(); i++) {
        usage += i->size();
    }
    return usage;
}

bool CommandQueue::HasRWDependency(const CMDIterator& cmd_it,
                                   const CMDQueue& queue) const {
    // Read after write has been checked in controller so we only
    // check write after read here
    for (auto it = queue.begin(); it != cmd_it; it++) {
        if (it->IsRead() && it->Row() == cmd_it->Row() &&
            it->Column() == cmd_it->Column() && it->Bank() == cmd_it->Bank() &&
            it->Bankgroup() == cmd_it->Bankgroup()) {
            return true;
        }
    }
    return false;
}

}  // namespace dramsim3
