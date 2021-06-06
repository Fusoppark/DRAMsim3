#include "controller.h"
#include <iomanip>
#include <iostream>
#include <limits>

namespace dramsim3 {

#ifdef THERMAL
Controller::Controller(int channel, const Config &config, const Timing &timing,
                       ThermalCalculator &thermal_calc)
#else
Controller::Controller(int channel, const Config &config, const Timing &timing)
#endif  // THERMAL
    : channel_id_(channel),
      clk_(0),
      config_(config),
      simple_stats_(config_, channel_id_),
      channel_state_(config, timing),
      cmd_queue_(channel_id_, config, channel_state_, simple_stats_),
      refresh_(config, channel_state_),
#ifdef THERMAL
      thermal_calc_(thermal_calc),
#endif  // THERMAL
      is_unified_queue_(config.unified_queue),
      row_buf_policy_(config.row_buf_policy == "CLOSE_PAGE"
                          ? RowBufPolicy::CLOSE_PAGE
                          : RowBufPolicy::OPEN_PAGE),
      last_trans_clk_(0),
      write_draining_(0) {
    if (is_unified_queue_) {
        unified_queue_.reserve(config_.trans_queue_size);
    } else {
        read_queue_.reserve(config_.trans_queue_size);
        write_buffer_.reserve(config_.trans_queue_size);
    }
    copy_queue_.reserve(config_.trans_queue_size);

#ifdef CMD_TRACE
    std::string trace_file_name = config_.output_prefix + "ch_" +
                                  std::to_string(channel_id_) + "cmd.trace";
    std::cout << "Command Trace write to " << trace_file_name << std::endl;
    cmd_trace_.open(trace_file_name, std::ofstream::out);
#endif  // CMD_TRACE
}

std::pair<AddressPair, int> Controller::ReturnDoneTrans(uint64_t clk) {
    auto it = return_queue_.begin();
    while (it != return_queue_.end()) {
        if (clk >= it->complete_cycle) {
            if (it->is_write) {
                simple_stats_.Increment("num_writes_done");
            } else if (it->is_copy){
                simple_stats_.Increment("num_copies_done");
            }
            else {
                simple_stats_.Increment("num_reads_done");
                simple_stats_.AddValue("read_latency", clk_ - it->added_cycle);
            }
            auto pair = std::make_pair(it->addr, it->is_write);
            it = return_queue_.erase(it);
            return pair;
        } else {
            ++it;
        }
    }
    return std::make_pair(-1, -1);
}

void Controller::ClockTick() {
    // update refresh counter
    refresh_.ClockTick();

    bool cmd_issued = false;
    Command cmd;
    if (channel_state_.IsRefreshWaiting()) {
        cmd = cmd_queue_.FinishRefresh();
    }

    // cannot find a refresh related command or there's no refresh
    if (!cmd.IsValid()) {
        //std::cout<<clk_<<" getcommandtoissue"<<std::endl;
        cmd = cmd_queue_.GetCommandToIssue();
    }

    if (cmd.IsValid()) {
        //std::cout<<clk_<<" "<<cmd.IsReadCopy()<<std::endl;
        IssueCommand(cmd);
        cmd_issued = true;

        if (config_.enable_hbm_dual_cmd) {
            auto second_cmd = cmd_queue_.GetCommandToIssue();
            if (second_cmd.IsValid()) {
                if (second_cmd.IsReadWrite() != cmd.IsReadWrite()) {
                    IssueCommand(second_cmd);
                    simple_stats_.Increment("hbm_dual_cmds");
                }
            }
        }
    }

    // power updates pt 1
    for (int i = 0; i < config_.ranks; i++) {
        if (channel_state_.IsRankSelfRefreshing(i)) {
            simple_stats_.IncrementVec("sref_cycles", i);
        } else {
            bool all_idle = channel_state_.IsAllBankIdleInRank(i);
            if (all_idle) {
                simple_stats_.IncrementVec("all_bank_idle_cycles", i);
                channel_state_.rank_idle_cycles[i] += 1;
            } else {
                simple_stats_.IncrementVec("rank_active_cycles", i);
                // reset
                channel_state_.rank_idle_cycles[i] = 0;
            }
        }
    }

    // power updates pt 2: move idle ranks into self-refresh mode to save power
    if (config_.enable_self_refresh && !cmd_issued) {
        for (auto i = 0; i < config_.ranks; i++) {
            if (channel_state_.IsRankSelfRefreshing(i)) {
                // wake up!
                if (!cmd_queue_.rank_q_empty[i]) {
                    auto addr = Address();
                    addr.rank = i;
                    auto cmd = Command(CommandType::SREF_EXIT, addr, -1);
                    cmd = channel_state_.GetReadyCommand(cmd, clk_);
                    if (cmd.IsValid()) {
                        IssueCommand(cmd);
                        break;
                    }
                }
            } else {
                if (cmd_queue_.rank_q_empty[i] &&
                    channel_state_.rank_idle_cycles[i] >=
                        config_.sref_threshold) {
                    auto addr = Address();
                    addr.rank = i;
                    auto cmd = Command(CommandType::SREF_ENTER, addr, -1);
                    cmd = channel_state_.GetReadyCommand(cmd, clk_);
                    if (cmd.IsValid()) {
                        IssueCommand(cmd);
                        break;
                    }
                }
            }
        }
    }

    ScheduleTransaction();
    clk_++;
    cmd_queue_.ClockTick();
    simple_stats_.Increment("num_cycles");
    //cmd_queue_.printFlag();
    //std::cout<<clk_<<" end"<<std::endl;
    return;
}

bool Controller::WillAcceptTransaction(AddressPair hex_addr, bool is_write) const {
    // Row Clone added
    if(hex_addr.is_copy){
        return copy_queue_.size() < copy_queue_.capacity();
    }

    if (is_unified_queue_) {
        return unified_queue_.size() < unified_queue_.capacity();
    } else if (!is_write) {
        return read_queue_.size() < read_queue_.capacity();
    } else {
        return write_buffer_.size() < write_buffer_.capacity();
    }
}

// Row Clone added
const Config* Controller::getConfig(){
    return &config_;
}

void Controller::InCopyFlagDown(){
    cmd_queue_.InCopyFlagDown();
}

bool Controller::AddTransaction(Transaction trans) {
    //std::cout<<clk_<<" addtransaction"<<std::endl;
    trans.added_cycle = clk_;
    simple_stats_.AddValue("interarrival_latency", clk_ - last_trans_clk_);
    last_trans_clk_ = clk_;
    
    // RowClone added
    if(trans.is_copy){ // if the transaction is copy operation
		if(pending_wr_q_.count(trans.addr) > 0){ // if src_addr write is in pending queue
            // write that value to dest_addr - change to write(dest_addr)
            Transaction new_trans = Transaction(trans.addr.dest_addr, true);
            if(is_unified_queue_){
                unified_queue_.push_back(new_trans);
            }
            else{
                write_buffer_.push_back(new_trans);
            }
            return true;
        }
        //std::cout<<"end check"<<std::endl;
        // new trans added to copy_queue_
        pending_cp_q_.insert(std::make_pair(trans.addr, trans));
        //std::cout<<pending_cp_q_.size()<<std::endl;
        
        if(pending_cp_q_.count(trans.addr) == 1){
            //std::cout<<"one"<<std::endl;
            copy_queue_.push_back(trans);
        }
        //std::cout<<"end add"<<std::endl;
        return true;
    }
    else if (trans.is_write) {
        if (pending_wr_q_.count(trans.addr) == 0) {  // can not merge writes
            pending_wr_q_.insert(std::make_pair(trans.addr, trans));
            if (is_unified_queue_) {
                unified_queue_.push_back(trans);
            } else {
                write_buffer_.push_back(trans);
            }
        }
        trans.complete_cycle = clk_ + 1;
        return_queue_.push_back(trans);
        return true;
    } else {  // read
        // if in write buffer, use the write buffer value
        if (pending_wr_q_.count(trans.addr) > 0) {
            trans.complete_cycle = clk_ + 1;
            return_queue_.push_back(trans);
            return true;
        }
        pending_rd_q_.insert(std::make_pair(trans.addr, trans));
        if (pending_rd_q_.count(trans.addr) == 1) {
            if (is_unified_queue_) {
                unified_queue_.push_back(trans);
            } else {
                read_queue_.push_back(trans);
            }
        }
        return true;
    }
}

void Controller::ScheduleTransaction() {
    // determine whether to schedule read or write
    if (write_draining_ == 0 && !is_unified_queue_) {
        // we basically have a upper and lower threshold for write buffer
        if ((write_buffer_.size() >= write_buffer_.capacity()) ||
            (write_buffer_.size() > 8 && cmd_queue_.QueueEmpty())) {
            write_draining_ = write_buffer_.size();
        }
    }

	
    // row clone added (about copy_queue_)
    std::vector<Transaction> &queue =
        is_unified_queue_ ? unified_queue_
                          : copy_queue_.size() > 0 ? copy_queue_
                          : write_draining_ > 0 ? write_buffer_: read_queue_;
    for (auto it = queue.begin(); it != queue.end(); it++) {
        // Row clone added
        if(it->is_copy){
            auto cmds = CopyTransToCommand(*it);
            auto cmd_read = cmds.first;
            auto cmd_write = cmds.second;
            //cmd_write.addr.rank = cmd_read.addr.rank;
            if(cmd_queue_.WillAcceptCommand(cmd_read.Rank(), cmd_read.Bankgroup(), cmd_read.Bank()) \
                && cmd_queue_.WillAcceptCommand(cmd_write.Rank(), cmd_write.Bankgroup(), cmd_write.Bank(), 1)){
                // TODO: write_draining_ 수정하기
                if (pending_rd_q_.count(it->addr.dest_addr) > 0) {
                    write_draining_ = 0;
                    break;
                }

                //std::cout<<cmd_read.addr.rank<<" "<<cmd_write.addr.rank<<std::endl;
                if(cmd_read.addr.bankgroup == cmd_write.addr.bankgroup && cmd_read.addr.bank == cmd_write.addr.bank){
                    cmd_read.isFPM = true;
                    cmd_write.isFPM = true;
                }
                else{
                    cmd_read.isFPM = false;
                    cmd_write.isFPM = false;
                }
                cmd_queue_.AddCommand(cmd_read);
                cmd_queue_.AddCommand(cmd_write);
                //std::cout<<clk_<<" "<<cmd_read.hex_addr.src_addr<<" added "<<cmd_write.hex_addr.dest_addr<<std::endl;
                queue.erase(it);
                break;
            }
        }
        else{
            auto cmd = TransToCommand(*it);
            if (cmd_queue_.WillAcceptCommand(cmd.Rank(), cmd.Bankgroup(),
                                            cmd.Bank())) {
                if (!is_unified_queue_ && cmd.IsWrite()) {
                    // Enforce R->W dependency
                    if (pending_rd_q_.count(it->addr) > 0) {
                        write_draining_ = 0;
                        break;
                    }
                    write_draining_ -= 1;
                }
                cmd_queue_.AddCommand(cmd);
                queue.erase(it);
                break;
            }
        }
    }
}

void Controller::IssueCommand(const Command &cmd) {
#ifdef CMD_TRACE
    cmd_trace_ << std::left << std::setw(18) << clk_ << " " << cmd << std::endl;
#endif  // CMD_TRACE
#ifdef THERMAL
    // add channel in, only needed by thermal module
    thermal_calc_.UpdateCMDPower(channel_id_, cmd, clk_);
#endif  // THERMAL

    // to get to know command's type
    auto source = config_.AddressMapping(cmd.hex_addr.src_addr);
    auto dest = config_.AddressMapping(cmd.hex_addr.dest_addr);
    std::cout<<clk_<<" ";
    switch(cmd.cmd_type){
        case CommandType::READ:
            std::cout<<"read"<<std::endl;
            break;
        case CommandType::READ_PRECHARGE:
            std::cout<<"read_precharge"<<std::endl;
            break;
        case CommandType::READCOPY:
            std::cout<<cmd.Rank()<<" "<<cmd.Bankgroup()<<" "<<cmd.Bank()<<" readcopy "<<dest.rank<<" "<<dest.bankgroup<<" "<<dest.bank<<std::endl;
            break;
        case CommandType::READCOPY_PRECHARGE:
            std::cout<<"readcopy_precharge"<<std::endl;
            break;
        case CommandType::WRITE:
            std::cout<<"write"<<std::endl;
            break;
        case CommandType::WRITE_PRECHARGE:
            std::cout<<"write_precharge"<<std::endl;
            break;
        case CommandType::WRITECOPY:
            std::cout<<source.rank<<" "<<source.bankgroup<<" "<<source.bank<<" writecopy "<<cmd.Rank()<<" "<<cmd.Bankgroup()<<" "<<cmd.Bank()<<std::endl;
            break;
        case CommandType::WRITECOPY_PRECHARGE:
            std::cout<<"writecopy_precharge"<<std::endl;
            break;
        case CommandType::ACTIVATE:
            std::cout<<"activate "<<cmd.Rank()<<" "<<cmd.Bankgroup()<<" "<<cmd.Bank()<<std::endl;
            break;
        case CommandType::PRECHARGE:
            std::cout<<"precharge "<<cmd.Rank()<<" "<<cmd.Bankgroup()<<" "<<cmd.Bank()<<std::endl;
            break;
        case CommandType::REFRESH:
            std::cout<<"refresh"<<std::endl;
            break;
        case CommandType::REFRESH_BANK:
            std::cout<<"refresh_bank"<<std::endl;
            break;
        case CommandType::SREF_ENTER:
            std::cout<<"sref_enter"<<std::endl;
            break;
        case CommandType::SREF_EXIT:
            std::cout<<"sref_exit"<<std::endl;
            break;
        case CommandType::SIZE:
            std::cout<<"error"<<std::endl;
    }
    // if read/write, update pending queue and return queue
    if (cmd.IsRead()) {
        auto num_reads = pending_rd_q_.count(cmd.hex_addr);
        if (num_reads == 0) {
            std::cerr << cmd.hex_addr << " not in read queue! " << std::endl;
            exit(1);
        }
        // if there are multiple reads pending return them all
        while (num_reads > 0) {
            auto it = pending_rd_q_.find(cmd.hex_addr);
            it->second.complete_cycle = clk_ + config_.read_delay;
            return_queue_.push_back(it->second);
            pending_rd_q_.erase(it);
            num_reads -= 1;
        }
    } else if (cmd.IsWrite()) {
        // there should be only 1 write to the same location at a time
        auto it = pending_wr_q_.find(cmd.hex_addr);
        if (it == pending_wr_q_.end()) {
            std::cerr << cmd.hex_addr << " not in write queue!" << std::endl;
            exit(1);
        }
        auto wr_lat = clk_ - it->second.added_cycle + config_.write_delay;
        simple_stats_.AddValue("write_latency", wr_lat);
        pending_wr_q_.erase(it);
    } else if (cmd.IsReadCopy()) { // rowclone added
        // find exactly same copy from pending_copy_queue
        // if there is, return it
        //std::cout<<clk_<<" isreadcopy "<<pending_cp_q_.size()<<std::endl;
        auto num_copys = pending_cp_q_.count(cmd.hex_addr);
        if (num_copys == 0) {
            std::cerr << cmd.hex_addr << " not in copy queue! " << std::endl;
            exit(1);
        }
        // if there are multiple reads pending return them all
        while (num_copys > 0) {
            auto it = pending_cp_q_.find(cmd.hex_addr);
            it->second.complete_cycle = clk_; // timing calculation
            return_queue_.push_back(it->second);
            pending_cp_q_.erase(it);
            num_copys -= 1;
        }
        //std::cout<<"issue command read copy"<<std::endl;
    }
    else if(cmd.IsWriteCopy()){
        // if writecopy
        // state update to wait writecopy
        InCopyFlagDown();
        //std::cout<<"issue writecopy"<<std::endl;
    }
    // must update stats before states (for row hits)
    UpdateCommandStats(cmd);
    channel_state_.UpdateTimingAndStates(cmd, clk_);
    // TODO : update timing (calculation...OTL)
}

Command Controller::TransToCommand(const Transaction &trans) {
    auto addr = config_.AddressMapping(trans.addr);
    CommandType cmd_type;
    if (row_buf_policy_ == RowBufPolicy::OPEN_PAGE) {
        cmd_type = trans.is_write ? CommandType::WRITE : CommandType::READ;
    } else {
        cmd_type = trans.is_write ? CommandType::WRITE_PRECHARGE
                                  : CommandType::READ_PRECHARGE;
    }
    return Command(cmd_type, addr, trans.addr);
}

// rowclone added
std::pair<Command, Command> Controller::CopyTransToCommand(const Transaction &trans){
    auto addr1 = config_.AddressMapping(trans.addr.src_addr); // for readcopy
    auto addr2 = config_.AddressMapping(trans.addr.dest_addr); // for writecopy

    //std::cout << "CopyTransToCommand : " << addr1.rank * config_.banks + addr1.bankgroup * config_.banks_per_group + addr1.bank << " " << addr2.rank * config_.banks + addr2.bankgroup * config_.banks_per_group + addr2.bank << std::endl;
    CommandType cmd_type1, cmd_type2;
    if (row_buf_policy_ == RowBufPolicy::OPEN_PAGE){
        cmd_type1 = CommandType::READCOPY;
        cmd_type2 = CommandType::WRITECOPY;
        //std::cout<<clk_<<" read write"<<std::endl;
    } else {
        cmd_type1 = CommandType::READCOPY_PRECHARGE;
        cmd_type2 = CommandType::WRITECOPY_PRECHARGE;
        //std::cout<<clk_<<" readpre writepre"<<std::endl;
    }
    return std::make_pair(Command(cmd_type1, addr1, trans.addr), Command(cmd_type2, addr2, trans.addr));
}

int Controller::QueueUsage() const { return cmd_queue_.QueueUsage(); }

void Controller::PrintEpochStats() {
    simple_stats_.Increment("epoch_num");
    simple_stats_.PrintEpochStats();
#ifdef THERMAL
    for (int r = 0; r < config_.ranks; r++) {
        double bg_energy = simple_stats_.RankBackgroundEnergy(r);
        thermal_calc_.UpdateBackgroundEnergy(channel_id_, r, bg_energy);
    }
#endif  // THERMAL
    return;
}

void Controller::PrintFinalStats() {
    simple_stats_.PrintFinalStats();

#ifdef THERMAL
    for (int r = 0; r < config_.ranks; r++) {
        double bg_energy = simple_stats_.RankBackgroundEnergy(r);
        thermal_calc_.UpdateBackgroundEnergy(channel_id_, r, bg_energy);
    }
#endif  // THERMAL
    return;
}

void Controller::UpdateCommandStats(const Command &cmd) {
    switch (cmd.cmd_type) {
        case CommandType::READ:
        case CommandType::READ_PRECHARGE:
            simple_stats_.Increment("num_read_cmds");
            if (channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(),
                                           cmd.Bank()) != 0) {
                simple_stats_.Increment("num_read_row_hits");
            }
            break;
        case CommandType::WRITE:
        case CommandType::WRITE_PRECHARGE:
            simple_stats_.Increment("num_write_cmds");
            if (channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(),
                                           cmd.Bank()) != 0) {
                simple_stats_.Increment("num_write_row_hits");
            }
            break;
        case CommandType::READCOPY:
        case CommandType::READCOPY_PRECHARGE:
            simple_stats_.Increment("num_read_copy_cmds");
            break;
        case CommandType::WRITECOPY:
        case CommandType::WRITECOPY_PRECHARGE:
            simple_stats_.Increment("num_write_copy_cmds");
            break;
        case CommandType::ACTIVATE:
            simple_stats_.Increment("num_act_cmds");
            break;
        case CommandType::PRECHARGE:
            simple_stats_.Increment("num_pre_cmds");
            break;
        case CommandType::REFRESH:
            simple_stats_.Increment("num_ref_cmds");
            break;
        case CommandType::REFRESH_BANK:
            simple_stats_.Increment("num_refb_cmds");
            break;
        case CommandType::SREF_ENTER:
            simple_stats_.Increment("num_srefe_cmds");
            break;
        case CommandType::SREF_EXIT:
            simple_stats_.Increment("num_srefx_cmds");
            break;
        default:
            AbruptExit(__FILE__, __LINE__);
    }
}

}  // namespace dramsim3
