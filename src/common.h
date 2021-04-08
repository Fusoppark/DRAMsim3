#ifndef __COMMON_H
#define __COMMON_H

#include <stdint.h>
#include <iostream>
#include <vector>

namespace dramsim3 {

// RowClone added
    class AddressPair {
    public:
        uint64_t src_addr, dest_addr;
        bool is_copy;

        AddressPair(const uint64_t src_addr, const uint64_t dest_addr)
                :src_addr(src_addr), dest_addr(dest_addr), is_copy(true) {}

        AddressPair(const uint64_t src_addr): src_addr(src_addr), dest_addr(0), is_copy(false) {}
        AddressPair() :src_addr(0), dest_addr(0), is_copy(false) {}

        operator uint64_t() const {
            return src_addr;
        }

        AddressPair& operator= (const AddressPair &a) {
            src_addr = a.src_addr;
            dest_addr = a.dest_addr;
            return *this;
        }

        AddressPair operator>> (const int a) {
            return AddressPair(src_addr >> a, dest_addr);
        }

        AddressPair operator<< (const int a) {
            return AddressPair(src_addr << a, dest_addr);
        }

        AddressPair operator>> (const uint64_t a) {
            return AddressPair(src_addr >> a, dest_addr);
        }

        AddressPair operator<< (const uint64_t a) {
            return AddressPair(src_addr << a, dest_addr);
        }

        AddressPair operator+= (const int a) {
            src_addr += a;
            return *this;
        }

        friend std::istream& operator>>(std::istream& is, AddressPair& addr) {
            is >> addr.src_addr;
            return is;
        }

        AddressPair& operator<<= (const uint64_t a) {
            src_addr = src_addr << a;
            return *this;
        }
        AddressPair& operator>>= (const uint64_t a) {
            src_addr = src_addr << a;
            return *this;
        }
    };

struct Address {
    Address()
        : channel(-1), rank(-1), bankgroup(-1), bank(-1), row(-1), column(-1) {}
    Address(int channel, int rank, int bankgroup, int bank, int row, int column)
        : channel(channel),
          rank(rank),
          bankgroup(bankgroup),
          bank(bank),
          row(row),
          column(column) {}
    Address(const Address& addr)
        : channel(addr.channel),
          rank(addr.rank),
          bankgroup(addr.bankgroup),
          bank(addr.bank),
          row(addr.row),
          column(addr.column) {}
    int channel;
    int rank;
    int bankgroup;
    int bank;
    int row;
    int column;
};

inline uint32_t ModuloWidth(AddressPair addr, uint32_t bit_width, uint32_t pos) {
    addr >>= pos;
    auto store = addr;
    addr >>= bit_width;
    addr <<= bit_width;
    return static_cast<uint32_t>(store ^ addr);
}

// extern std::function<Address(uint64_t)> AddressMapping;
int GetBitInPos(uint64_t bits, int pos);
// it's 2017 and c++ std::string still lacks a split function, oh well
std::vector<std::string> StringSplit(const std::string& s, char delim);
template <typename Out>
void StringSplit(const std::string& s, char delim, Out result);

int LogBase2(int power_of_two);
void AbruptExit(const std::string& file, int line);
bool DirExist(std::string dir);

enum class CommandType {
    READ,
    READ_PRECHARGE,
    READCOPY,
    READCOPY_PRECHARGE,
    WRITE,
    WRITE_PRECHARGE,
    WRITECOPY,
    WRITECOPY_PRECHARGE,
    ACTIVATE,
    PRECHARGE,
    REFRESH_BANK,
    REFRESH,
    SREF_ENTER,
    SREF_EXIT,
    SIZE
};

struct Command {
    Command() : cmd_type(CommandType::SIZE), hex_addr(0) {}
    Command(CommandType cmd_type, const Address& addr, AddressPair hex_addr)
        : cmd_type(cmd_type), addr(addr), hex_addr(hex_addr) {}
    // Command(const Command& cmd) {}

    bool IsValid() const { return cmd_type != CommandType::SIZE; }
    bool IsRefresh() const {
        return cmd_type == CommandType::REFRESH ||
               cmd_type == CommandType::REFRESH_BANK;
    }
    bool IsRead() const {
        return cmd_type == CommandType::READ ||
               cmd_type == CommandType ::READ_PRECHARGE;
    }
    bool IsWrite() const {
        return cmd_type == CommandType ::WRITE ||
               cmd_type == CommandType ::WRITE_PRECHARGE;
    }
    bool IsReadWrite() const { return IsRead() || IsWrite(); }
    bool IsRankCMD() const {
        return cmd_type == CommandType::REFRESH ||
               cmd_type == CommandType::SREF_ENTER ||
               cmd_type == CommandType::SREF_EXIT;
    }
    CommandType cmd_type;
    Address addr;
    AddressPair hex_addr;

    int Channel() const { return addr.channel; }
    int Rank() const { return addr.rank; }
    int Bankgroup() const { return addr.bankgroup; }
    int Bank() const { return addr.bank; }
    int Row() const { return addr.row; }
    int Column() const { return addr.column; }

    friend std::ostream& operator<<(std::ostream& os, const Command& cmd);
};

struct Transaction {
    Transaction() {}
    Transaction(AddressPair addr, bool is_write)
        : addr(addr),
          added_cycle(0),
          complete_cycle(0),
          is_write(is_write),
          is_copy(addr.is_copy) {}
    Transaction(const Transaction& tran)
        : addr(tran.addr),
          added_cycle(tran.added_cycle),
          complete_cycle(tran.complete_cycle),
          is_write(tran.is_write),
          is_copy(tran.is_copy) {}
    AddressPair addr;
    uint64_t added_cycle;
    uint64_t complete_cycle;
    bool is_write;

    // Row Clone added
    bool is_copy;

    friend std::ostream& operator<<(std::ostream& os, const Transaction& trans);
    friend std::istream& operator>>(std::istream& is, Transaction& trans);
};

}  // namespace dramsim3
#endif
