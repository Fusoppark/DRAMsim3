[![Build Status](https://travis-ci.com/umd-memsys/DRAMsim3.svg?branch=master)](https://travis-ci.com/umd-memsys/DRAMsim3)

# What is Fusoppark / DRAMsim3 ?
 - Dram Simulator [DRAMsim3](https://github.com/umd-memsys/DRAMsim3)에 Copy Operation을 구현한 프로젝트의 결과물입니다.
 - 원래 프로젝트는 [이쪽](https://github.com/ChoSeokJu/DRAMsim3)으로, 이곳은 포트폴리오를 위해 따로 fork한 곳입니다.
 
 ## Contribution of Fusoppark
  - src/commandqueue.cc : Copy Operation은 ReadCopy 이후 바로 WriteCopy를 수행하도록 강제하는데, 이를 가능케하는 로직을 구현했습니다. 
  - src/channelstate.cc : Copy Operation의 수행시간을 계산하기 위해 command를 구분하는 코드를 추가하였습니다.
  - src/bankstate.cc : Copy Operation의 수행시간을 계산하기 위해 command를 구분하는 코드를 추가하였습니다.
  - src/timing.cc : Copy Operation의 수행시간을 계산하기 위한 value들을 추가하였습니다.

 ## Detail
  - 현재 작성 중입니다. 작성되는 대로 추가하겠습니다.



# What we have done...

- `bankstate.cc` / `bankstate.h`

   writecopy, readcopy를 위한 activate, precharge, readcopy, writecopy issue

- `channelstate.cc` / `channelstate.h` 

  writecopy와 readcopy의 순서를 제한하기 위한 연산 구현

  FPM, PSM 인지 판단해서 timing 계산하는 코드

- `commnad_queue.cc` / `command_queue.h` 

  copy queue를 만들고 해당 copy를 이슈하도록 조절

- `common.cc`

  Address Pair를 만들어 두 address를 처리할 수 있게 함

- `configuration.cc`

  readcopy, writecopy등의 command와 copy transaction추가

- `controller.cc` / `controller.h` 

  PIM을 위한 전반적인 연산의 총괄

- `cpu.cc` / `cpu.h`

  **stream CPU** : 하나의 array에서 random sampling하는 과정 추가

  **random CPU** : copy operation만 random하게 생성하도록 함

- `memory_system.cc` / `memory_system.h`

  copy transaction을 위해 willaccept함수와 add함수 구현

- `timing.cc` / `timing.h`

  Readcopy WriteCopy FPM, PSM 값 지정 

- `dram_system.cc` / `dram_system.h`

  Copy transaction을 위해 willaccept함수와 add함수 구현

### ColClone

DRAM에서 copy를 수행하는 Transaction으로 Rowclone에서 파생된 **Colclone**을 정의하였다. **Colclone**이란, Source Address의 column에 저장된 data를 destination address의 column에 복사하는 transaction이다. Copy는 Transaction으로써, Readcopy command와 Wrtiecopy command로 구성되어진다.

- FPM

  Source column이 속한 Bank와 Destination column이 속한 Bank가 같은 경우에서의 동작.Rowclone에서와 같이 deactivate라는 command를 정의하여, precharge command와 동일하게 동작하나 Sense amplifier를 초기화하지 않는 Command를 의미한다. Deactivate와 Precharge는 서로 동작과 timing이 유사하기 때문에, 실제 구현에서는 deactivate command를 구현하지 않고, precharge하는 것으로 대체하였다.

- PSM

  Source column이 속한 bank와 Destination column이 속한 bank가 다른 경우에서의 동작.



- **DDR3용이다!**

  현재 Bankgroup이 다른 경우의 ColClone은 구현되어 있지 않다. DDR4부터 BankGroup을 나누기 때문에 DDR4로는 동작하지 않을 수 있다.



### Modifications

1. class `AddressPair` & type cast

   - `AddressPair` definition

     해당 transaction또는 command의 address를 저장하고 있는다. 하나의 address를 갖는 read와 write의 경우는 src address 부분에 해당 address를 저장하고 있다. Copy의 경우 두개의 address를 가지므로 src address와 dest address부분에 모두 주소를 저장하고 `is_copy` flag를 올림으로써 해당 command/transaction이 copy임을 표시한다

   - how `Address` replaced to `AddressPair`

     = 연산자 오버로딩을 통해 하나의 address를 필요로 하는 연산에서는 src address만 return하도록 하여 모든 uint64_t address를 AddressPair의 타입을 갖도록 수정해 주었다.

2. `COPY` transaction

   - how Copy transaction is implemented

     Copy transaction을 `configuration.cc`에 추가하고, `common.cc`에서 Transaction class가 `uint64_t` addr대신 `AddressPair` addr를 갖도록 수정하여 Copy transaction을 구현하였다.

3. `COPY` transaction handle

   - `WillAcceptTransaction`

     copy queue의 capacity에 따라 capacity 내에 accept되어질 수 있다면 true를 리턴하도록 한다.

   - `AddTransaction`

     copy queue의 가장 뒤에 push하여 add한다.

   - `ScheduleTransaction ` -> `TransToCommand`

     FCFS의 스케쥴링 정책에 따라 선택되어진 transaction에 해당되는 readcopy, writecopy를 모두 add할 수 있을 때 하나의 Copy transaction에서 readcopy, writecopy의 command로 나누어 해당 bank의 command queue의 가장 뒤로 push 되어진다.

4. `ReadCopy`, `WriteCopy` command

   - Implementation details

     Command의 type으로 `ReadCopy`, `WriteCopy`, `ReadCopy_Precharge`, `WriteCopy_Precharge`를 추가하여 구현하였다. FPM, PSM은 command 상에서는 구분하지 않고, timing을 계산할 때만 구분한다.

   - state 변환

     activate를 하면 해당 row가 open되고, precharge를 하게 되면 해당 row가 close되어진다. writecopy가 일어나는 column에서 activate까지 완료하고 readcopy가 끝난상태이면 wait writecopy state로 들어가게 된다.

   - timing (FPM / PSM 구분은 여기서 함)

     ReadCopy(or ReadCopy_Precharge)는 Read와 같다고 취급하여 timing을 계산하였다. WriteCopy(or WriteCopy_Precharge)는 Write와 같다고 취급하여 timing을 계산하였다. 그러나 Read/Write와는 다르게, FPM의 경우 ReadCopy/WriteCopy는 Rank 내 bus를 쓰지 않으므로 other bank same bankgroup, other bankgroup same rank, other rank의 bank들의 timing에 관여하지 않는 점을 반영하여 timing을 0으로 주었다. PSM의 경우 ReadCopy/WriteCopy는 Off Chip Bus를 사용하지 않기 때문에 other rank의 bank들의 timing에 관여하지 않는 점을 반영하여 timing을 0으로 주었다

5. `ReadCopy`, `WriteCopy` command handle

   - `ReadCopy` command issue condition

     command queue의 가장 앞에 있는 readcopy에 대하여 issue를 시작하도록 한다.

   - How we issue `WriteCopy` right after `ReadCopy`

     writecopy에 대해서 wait writecopy state일 때 write copy를 issue하도록 한다. wait writecopy state는 readcopy가 끝나고, writecopy를 이슈 가능한 상태이기 때문에 바로 issue 가능해진다. readcopy를 수행할 때 다음으로 writecopy를 수행할 row에 대하여 매 cycle tracking을 하여 wait_writecopy state인지를 확인하는 과정을 거친다.

6. CPU

   - RandomCPU (`clocktick()`, `getRandomAddress()`)

     `getRandomAddress()` 함수에서 같은 rank의 데이터 위치를 랜덤하게 2개를 생성하도록 하고, 해당 address를 src, dest로 각각 가지는 copy를 output하도록 한다

   - StreamCPU (`clocktick()` : 시뮬레이션용)

     시뮬레이션을 위해 하나의 array를 선언하고, 일정 개수 만큼의 element를 랜덤으로 뽑도록 한다. 여기서 DRAMsim3는 데이터를 하나의 row를 단위로 저장하기 때문에 여러개의 rank에 데이터가 저장되고, 랜덤으로 선택한 element의 rank에 맞추어 해당 rank별 데이터를 모을 공간을 hardcoding하여 지정해 주었다.

### Unresolved Errors

현재 Copy transaction과 ReadCopy, WriteCopy command는 issue가 된다. 그러나 issue되는 시점에 오류가 있는데, 바로 DRAMsim3의 precharge 정책과 충돌하는 것이다. 

# About DRAMsim3

DRAMsim3 models the timing paramaters and memory controller behavior for several DRAM protocols such as DDR3, DDR4, LPDDR3, LPDDR4, GDDR5, GDDR6, HBM, HMC, STT-MRAM. It is implemented in C++ as an objected oriented model that includes a parameterized DRAM bank model, DRAM controllers, command queues and system-level interfaces to interact with a CPU simulator (GEM5, ZSim) or trace workloads. It is designed to be accurate, portable and parallel.
    
If you use this simulator in your work, please consider cite:

[1] S. Li, Z. Yang, D. Reddy, A. Srivastava and B. Jacob, "DRAMsim3: a Cycle-accurate, Thermal-Capable DRAM Simulator," in IEEE Computer Architecture Letters. [Link](https://ieeexplore.ieee.org/document/8999595)

See [Related Work](#related-work) for more work done with this simulator.


## Building and running the simulator

This simulator by default uses a CMake based build system.
The advantage in using a CMake based build system is portability and dependency management.
We require CMake 3.0+ to build this simulator.
If `cmake-3.0` is not available,
we also supply a Makefile to build the most basic version of the simulator.

### Building

Doing out of source builds with CMake is recommended to avoid the build files cluttering the main directory.

```bash
# cmake out of source build
mkdir build
cd build
cmake ..

# Build dramsim3 library and executables
make -j4

# Alternatively, build with thermal module enabled
cmake .. -DTHERMAL=1

```

The build process creates `dramsim3main` and executables in the `build` directory.
By default, it also creates `libdramsim3.so` shared library in the project root directory.

### Running

```bash
# help
./build/dramsim3main -h

# Running random stream with a config file
./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini --stream random -c 100000 

# Running a trace file
./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini -c 100000 -t sample_trace.txt

# Running with gem5
--mem-type=dramsim3 --dramsim3-ini=configs/DDR4_4Gb_x4_2133.ini

```

The output can be directed to another directory by `-o` option
or can be configured in the config file.
You can control the verbosity in the config file as well.

### Output Visualization

`scripts/plot_stats.py` can visualize some of the output (requires `matplotlib`):

```bash
# generate histograms from overall output
python3 scripts/plot_stats dramsim3.json

# or
# generate time series for a variety stats from epoch outputs
python3 scripts/plot_stats dramsim3epoch.json
```

Currently stats from all channels are squashed together for cleaner plotting.

### Integration with other simulators

**Gem5** integration: works with a forked Gem5 version, see https://github.com/umd-memsys/gem5 at `dramsim3` branch for reference.

**SST** integration: see http://git.ece.umd.edu/shangli/sst-elements/tree/dramsim3 for reference. We will try to merge to official SST repo.

**ZSim** integration: see http://git.ece.umd.edu/shangli/zsim/tree/master for reference.

## Simulator Design

### Code Structure

```
├── configs                 # Configs of various protocols that describe timing constraints and power consumption.
├── ext                     # 
├── scripts                 # Tools and utilities
├── src                     # DRAMsim3 source files
├── tests                   # Tests of each model, includes a short example trace
├── CMakeLists.txt
├── Makefile
├── LICENSE
└── README.md

├── src  
    bankstate.cc: Records and manages DRAM bank timings and states which is modeled as a state machine.
    channelstate.cc: Records and manages channel timings and states.
    command_queue.cc: Maintains per-bank or per-rank FIFO queueing structures, determine which commands in the queues can be issued in this cycle.
    configuration.cc: Initiates, manages system and DRAM parameters, including protocol, DRAM timings, address mapping policy and power parameters.
    controller.cc: Maintains the per-channel controller, which manages a queue of pending memory transactions and issues corresponding DRAM commands, 
                   follows FR-FCFS policy.
    cpu.cc: Implements 3 types of simple CPU: 
            1. Random, can handle random CPU requests at full speed, the entire parallelism of DRAM protocol can be exploited without limits from address mapping and scheduling pocilies. 
            2. Stream, provides a streaming prototype that is able to provide enough buffer hits.
            3. Trace-based, consumes traces of workloads, feed the fetched transactions into the memory system.
    dram_system.cc:  Initiates JEDEC or ideal DRAM system, registers the supplied callback function to let the front end driver know that the request is finished. 
    hmc.cc: Implements HMC system and interface, HMC requests are translates to DRAM requests here and a crossbar interconnect between the high-speed links and the memory controllers is modeled.
    main.cc: Handles the main program loop that reads in simulation arguments, DRAM configurations and tick cycle forward.
    memory_system.cc: A wrapper of dram_system and hmc.
    refresh.cc: Raises refresh request based on per-rank refresh or per-bank refresh.
    timing.cc: Initiate timing constraints.
```

## Experiments

### Verilog Validation

First we generate a DRAM command trace.
There is a `CMD_TRACE` macro and by default it's disabled.
Use `cmake .. -DCMD_TRACE=1` to enable the command trace output build and then
whenever a simulation is performed the command trace file will be generated.

Next, `scripts/validation.py` helps generate a Verilog workbench for Micron's Verilog model
from the command trace file.
Currently DDR3, DDR4, and LPDDR configs are supported by this script.

Run

```bash
./script/validataion.py DDR4.ini cmd.trace
```

To generage Verilog workbench.
Our workbench format is compatible with ModelSim Verilog simulator,
other Verilog simulators may require a slightly different format.


## Related Work

[1] Li, S., Yang, Z., Reddy D., Srivastava, A. and Jacob, B., (2020) DRAMsim3: a Cycle-accurate, Thermal-Capable DRAM Simulator, IEEE Computer Architecture Letters.

[2] Jagasivamani, M., Walden, C., Singh, D., Kang, L., Li, S., Asnaashari, M., ... & Yeung, D. (2019). Analyzing the Monolithic Integration of a ReRAM-Based Main Memory Into a CPU's Die. IEEE Micro, 39(6), 64-72.

[3] Li, S., Reddy, D., & Jacob, B. (2018, October). A performance & power comparison of modern high-speed DRAM architectures. In Proceedings of the International Symposium on Memory Systems (pp. 341-353).

[4] Li, S., Verdejo, R. S., Radojković, P., & Jacob, B. (2019, September). Rethinking cycle accurate DRAM simulation. In Proceedings of the International Symposium on Memory Systems (pp. 184-191).

[5] Li, S., & Jacob, B. (2019, September). Statistical DRAM modeling. In Proceedings of the International Symposium on Memory Systems (pp. 521-530).

[6] Li, S. (2019). Scalable and Accurate Memory System Simulation (Doctoral dissertation).
