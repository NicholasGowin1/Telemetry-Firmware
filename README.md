# 📦 FSAE Telemetry (`disectCAN`)

## 🌟 Highlights

- Turns raw CAN messages into named, labeled telemetry entries
- Looks up every message in a simple `CAN_ID_Reference.txt` file, so adding or renaming a message never requires a code change
- Keeps messages sorted by priority automatically
- Writes everything out to `write.txt` and frees its memory in one call
- Plain C++ with no external dependencies

## ℹ️ Overview

`disectCAN` is a small C++ library that takes the CAN messages coming off the car, matches each one against a reference file (name, bus, priority, signedness, whether it is a bitfield), and keeps them in a priority-ordered list. That list can be printed or written to a text file, which frees the memory as it goes.

It was built for the telemetry system on our Formula SAE electric car, and is designed to run on a Raspberry Pi reading live frames from the car's CAN buses.

**Current status:** messages are matched by CAN ID, and the payload is treated as a single number per message. Decoding individual signals out of each message's bytes is still in progress.
**Future plans:** The next steps are to push CAN data through a Raspberry Pi 5 and disect that data live into a file. After that we'll be able to send the decoded messages to the car's dashboard and upload it to a live server via cellular. This server will then be able to be accessed remotely, and the data will be visualized

### ✍️ Authors

I'm [Nicholas Gowin](https://github.com/NicholasGowin1). I'm writing the telemetry firmware for CU Boulder's Formula SAE CBE1/CBE2 so that we can receive, sort, and log live data from the car.

## 🚀 Usage

```cpp
disectCan can;
can.getCanLine(messages);   // messages are {canID, value} pairs
can.writeCanData();         // writes write.txt in priority order, then frees memory
```

Each entry in `CAN_ID_Reference.txt` is one line, comma separated:
(make sure to upadte file as needed)

```
name,bitfield,onCan1,isSigned,priority,canID
Torque Command - Front,0,1,1,6,0x105
```

`bitfield`, `onCan1` and `isSigned` are `1` or `0`. The `canID` can be decimal or hex (`0x105`).

## ⬇️ Installation

Requires a C++11 (or newer) compiler such as `clang++` or `g++`.

```bash
git clone <your-repo-url>
cd <your-repo-folder>
clang++ -std=c++17 main.cpp telemetry.cpp -o app
./app
```

Run the program from the folder that contains `CAN_ID_Reference.txt`, since that is where it looks for the file.
