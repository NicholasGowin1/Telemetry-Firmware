#include "can_bus.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#ifdef __linux__
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// ==================== SocketCanBus ====================

SocketCanBus::SocketCanBus(const std::string &ifname)
{
#ifdef __linux__
    fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd_ < 0)
        return;

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    if (ioctl(fd_, SIOCGIFINDEX, &ifr) < 0)
    {
        close(fd_);
        fd_ = -1;
        return;
    }

    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        close(fd_);
        fd_ = -1;
    }
#else
    (void)ifname; // SocketCAN is Linux-only; use SharedVirtualCanBus elsewhere
    fd_ = -1;
#endif
}

SocketCanBus::~SocketCanBus()
{
#ifdef __linux__
    if (fd_ >= 0)
        close(fd_);
#endif
}

bool SocketCanBus::send(const CanFrame &frame)
{
#ifdef __linux__
    if (fd_ < 0)
        return false;

    struct can_frame cf{};
    cf.can_id = frame.id & (frame.isExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
    if (frame.isExtended)
        cf.can_id |= CAN_EFF_FLAG;
    cf.can_dlc = frame.dlc > 8 ? 8 : frame.dlc;
    std::memcpy(cf.data, frame.data, cf.can_dlc);

    return ::write(fd_, &cf, sizeof(cf)) == static_cast<ssize_t>(sizeof(cf));
#else
    (void)frame;
    return false;
#endif
}

bool SocketCanBus::receive(CanFrame &frame, int timeoutMs)
{
#ifdef __linux__
    if (fd_ < 0)
        return false;

    struct pollfd pfd{fd_, POLLIN, 0};
    int r = poll(&pfd, 1, timeoutMs);
    if (r <= 0)
        return false; // timeout, or interrupted/error - either way, no frame

    struct can_frame cf{};
    ssize_t n = ::read(fd_, &cf, sizeof(cf));
    if (n != static_cast<ssize_t>(sizeof(cf)))
        return false;

    frame.isExtended = (cf.can_id & CAN_EFF_FLAG) != 0;
    frame.id = cf.can_id & (frame.isExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
    frame.dlc = cf.can_dlc;
    std::memcpy(frame.data, cf.data, cf.can_dlc);
    return true;
#else
    (void)frame;
    (void)timeoutMs;
    return false;
#endif
}

// ==================== SharedVirtualCanBus ====================

namespace
{
    struct Subscriber
    {
        std::mutex m;
        std::condition_variable cv;
        std::deque<CanFrame> queue;
    };

    struct ChannelState
    {
        std::mutex membersMutex;
        std::vector<std::shared_ptr<Subscriber>> members;
    };

    std::mutex g_registryMutex;
    std::map<std::string, std::shared_ptr<ChannelState>> g_registry;

    std::shared_ptr<ChannelState> getChannel(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto &ptr = g_registry[name];
        if (!ptr)
            ptr = std::make_shared<ChannelState>();
        return ptr;
    }
}

struct SharedVirtualCanBus::Impl
{
    std::shared_ptr<ChannelState> channel;
    std::shared_ptr<Subscriber> self;
};

SharedVirtualCanBus::SharedVirtualCanBus(const std::string &channelName) : impl_(new Impl)
{
    impl_->channel = getChannel(channelName);
    impl_->self = std::make_shared<Subscriber>();
    std::lock_guard<std::mutex> lock(impl_->channel->membersMutex);
    impl_->channel->members.push_back(impl_->self);
}

SharedVirtualCanBus::~SharedVirtualCanBus()
{
    {
        std::lock_guard<std::mutex> lock(impl_->channel->membersMutex);
        auto &members = impl_->channel->members;
        members.erase(std::remove(members.begin(), members.end(), impl_->self), members.end());
    }
    delete impl_;
}

bool SharedVirtualCanBus::send(const CanFrame &frame)
{
    std::lock_guard<std::mutex> lock(impl_->channel->membersMutex);
    for (auto &m : impl_->channel->members)
    {
        if (m == impl_->self)
            continue; // don't hear your own frames, like a real bus
        {
            std::lock_guard<std::mutex> qlock(m->m);
            m->queue.push_back(frame);
        }
        m->cv.notify_one();
    }
    return true;
}

bool SharedVirtualCanBus::receive(CanFrame &frame, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(impl_->self->m);
    if (impl_->self->queue.empty())
    {
        auto hasFrame = [&] { return !impl_->self->queue.empty(); };
        if (timeoutMs < 0)
            impl_->self->cv.wait(lock, hasFrame);
        else if (!impl_->self->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), hasFrame))
            return false; // timeout
    }
    frame = impl_->self->queue.front();
    impl_->self->queue.pop_front();
    return true;
}
