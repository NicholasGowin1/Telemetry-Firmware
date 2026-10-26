// can_bus.h
//
// A tiny CAN transport abstraction so Monitor and Simulator don't care
// whether they're talking to a real bus or a fake one:
//
//   - SocketCanBus       real or kernel-virtual CAN, Linux SocketCAN
//                        (can0, vcan0, ...). Needs root once to bring the
//                        interface up, nothing special after that.
//   - SharedVirtualCanBus  in-process virtual bus, no root/kernel module
//                        needed at all: every instance constructed with
//                        the same channel name shares traffic with every
//                        other one in this process. This is what --demo
//                        mode uses, mirroring python-can's "virtual"
//                        interface (see Monitor.py/Simulator.py).
//
// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 24, 2026
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct CanFrame
{
    uint32_t id = 0;         // arbitration id, already masked to 11 or 29 bits
    bool isExtended = false; // 29-bit (EFF) vs 11-bit (SFF)
    uint8_t dlc = 0;         // 0-8 bytes of payload actually used
    uint8_t data[8] = {0};
};

class CanBus
{
public:
    virtual ~CanBus() = default;
    virtual bool send(const CanFrame &frame) = 0;

    // Blocks up to timeoutMs waiting for one frame. Returns false on a
    // plain timeout (not an error) so callers can poll a stop flag in a
    // loop; pass -1 to block forever.
    virtual bool receive(CanFrame &frame, int timeoutMs) = 0;
};

// Real (or kernel-virtual, e.g. vcan0) SocketCAN interface. Linux only -
// on any other OS isOpen() stays false and send/receive just return
// false, so callers should check isOpen() right after construction.
class SocketCanBus : public CanBus
{
public:
    explicit SocketCanBus(const std::string &ifname);
    ~SocketCanBus() override;

    bool send(const CanFrame &frame) override;
    bool receive(CanFrame &frame, int timeoutMs) override;
    bool isOpen() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

// In-process virtual bus for --demo mode. No kernel module, no root, no
// network: two instances constructed with the same channel name (e.g.
// "demo") exchange frames through a small static registry. A bus never
// receives its own transmitted frames, matching a real bus's behaviour
// (and python-can's virtual interface with receive_own_messages=False).
class SharedVirtualCanBus : public CanBus
{
public:
    explicit SharedVirtualCanBus(const std::string &channel);
    ~SharedVirtualCanBus() override;

    bool send(const CanFrame &frame) override;
    bool receive(CanFrame &frame, int timeoutMs) override;

private:
    struct Impl;
    Impl *impl_; // pimpl: keeps <mutex>/<condition_variable> out of this header
};
