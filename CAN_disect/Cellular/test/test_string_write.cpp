#include "String_Write.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <algorithm>

int main()
{
    std::remove("text_files/CAN_Write.can"); // start clean
    {
        std::ofstream hdr("text_files/CAN_Write.can");
        hdr << "DATE|TIME|ID|LOCATION|MESSAGE NAME|MESSAGE\n";
    }

    StringWriter w;
    w.setFileName("text_files/CAN_Write.can");

    const std::string line1 =
        "2026-10-23 10:14:26.974  0x22  INV_FR_Cmd_SetACCurrent  ACCurrent=-87.9A";
    const std::string line2 =
        "2026-10-23 10:14:26.975  0x443  INV_RL_General3_Temps_Fault  "
        "FaultCode=NO FAULTS  MotorTemp=97.3degC  ControllerTemp=83.6degC";
    const std::string line3 =
        "2026-10-23 10:14:26.974  0x482  INV_FR_General5_IO_Limits  CANMapVersion=80  "
        "RPMMinLimit=0  RPMMaxLimit=0  PowerLimit=0  CapacitorTempLimit=0  DCCurrentLimit=0  "
        "DriveEnableLimit=1  IGBTAccelTempLimit=0  IGBTTempLimit=0  InputVoltageLimit=0  "
        "MotorAccelTempLimit=0  MotorTempLimit=0  DriveEnable=0  DigitalInput1=0  "
        "DigitalInput2=0  DigitalInput3=0  DigitalInput4=0  DigitalOutput1=0  "
        "DigitalOutput2=0  DigitalOutput3=0  DigitalOutput4=0  BrakeSignal=-125%  "
        "ThrottleSignal=-123%";

    assert(w.writeFile(line1));
    assert(w.writeFile(line2));
    assert(w.writeFile(line3));

    std::ifstream in("text_files/CAN_Write.can");
    std::string header, out1, out2, out3, extra;
    std::getline(in, header); // new: skip/check the header line
    std::getline(in, out1);
    std::getline(in, out2);
    std::getline(in, out3);
    bool hasExtra = static_cast<bool>(std::getline(in, extra));

    std::cout << out1 << "\n"
              << out2 << "\n"
              << out3 << "\n";

    // 1) simplest case: one pair
    assert(out1 == "2026-10-23 10:14:26.974|0x22|INV_FR_Cmd_SetACCurrent|ACCurrent|-87.9A");

    // 2) lone space inside a value, and no pairs carried over from line 1
    assert(out2 == "2026-10-23 10:14:26.975|0x443|INV_RL_General3_Temps_Fault|"
                   "FaultCode|NO FAULTS|MotorTemp|97.3degC|ControllerTemp|83.6degC");

    // 3) long line: 3 header fields + 23 pairs = 3 + 2*23 = 49 separators
    assert(std::count(out3.begin(), out3.end(), '|') == 48);
    assert(out3.rfind("2026-10-23 10:14:26.974|0x482|INV_FR_General5_IO_Limits|CANMapVersion|80|", 0) == 0);

    const std::string tail = "|BrakeSignal|-125%|ThrottleSignal|-123%";
    assert(out3.size() >= tail.size() &&
           out3.compare(out3.size() - tail.size(), tail.size(), tail) == 0);

    assert(!hasExtra);

    assert(!hasExtra); // exactly three lines, no blank ones
    std::cout << "All checks passed.\n";
}