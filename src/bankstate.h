#ifndef __BANKSTATE_H
#define __BANKSTATE_H

#include <vector>
#include "common.h"

namespace dramsim3 {

class BankState {
   public:
    BankState();

    enum class State { OPEN, CLOSED, SREF, PD, WAIT_WRITECOPY, SIZE };
    Command GetReadyCommand(const Command& cmd, uint64_t clk) const;

    // Update the state of the bank resulting after the execution of the command
    void UpdateState(const Command& cmd);

    // Update the existing timing constraints for the command
    void UpdateTiming(const CommandType cmd_type, uint64_t time);

    bool IsRowOpen() const { return state_ == State::OPEN; }
    int OpenRow() const { return open_row_; }
    int RowHitCount() const { return row_hit_count_; }

    // rowclone added
    void StartWaitWriteCopy(const Command& cmd);
    void FPMWaitWritecopy(const Command& cmd);
    bool isRightCommand(const Command& cmd) const;

   private:
    // Current state of the Bank
    // Apriori or instantaneously transitions on a command.
    State state_;

    // Earliest time when the particular Command can be executed in this bank
    std::vector<uint64_t> cmd_timing_;

    // Currently open row
    int open_row_;

    // consecutive accesses to one row
    int row_hit_count_;

    // rowcloe added
    Command waiting_command_;
    State wait_prev_state_; // state before going into wait_writecopy state
};

}  // namespace dramsim3
#endif
