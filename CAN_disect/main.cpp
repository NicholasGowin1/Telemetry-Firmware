#include "telemetry.h"

using namespace std;

int main()
{
    disectCan can;
    // Code generated/assisted by Google Geminai Pro
    // Date: September 18, 2026

    // Fields: {canID, value}
    vector<CanFrame> messages = {
        {256, 1.0},     // ShutdownCircuitFault (AIR Status)
        {257, 0.0},     // BMSFaultStatus (Pack Fault Flags)
        {258, 1250.0},  // IMDFaultStatus (Insulation Resistance)
        {260, 2.0},     // VCUHeartbeatState (DriveState)
        {261, 42.5},    // TorqueCommandFront (TorqueRequestFL)
        {263, 3450.0},  // MotorControllerStatusFL (MotorRPM)
        {264, 3480.0},  // MotorControllerStatusFR (MotorRPM)
        {267, 388.4},   // PackVoltageCurrent (PackVoltage)
        {268, 3.65},    // PackCellVoltageSummary (MinCellV)
        {269, 32.1},    // PackTemperatureSummary (AvgPackTemp)
        {270, 185.0},   // BrakePressure (FrontBrakePressure)
        {271, 24.5},    // APPSPedalPosition (APPS1Pct)
        {656912, 14.2}, // PDUStatusCh14 (PDMMeasuredCurrent01)

        {999, 0.0},   // Unmapped CAN ID (should trigger unknown ID warning)
        {12, 1.0},    // InverterDriveEnableCommand (High priority single value)
        {267, -15.5}, // PackVoltageCurrent (Negative current / regeneration test)
        {263, 0.0},   // Motor RPM zero
        {656944, 100.0}

    };

    can.getCanLine(messages);

    cout << "Messages in priority order:" << endl;
    can.printMessage();

    can.writeCanData();
    cout << "\nWrote messages to write.txt" << endl;

    return 0;
}