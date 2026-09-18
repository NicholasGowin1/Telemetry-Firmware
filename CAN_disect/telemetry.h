#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

using namespace std;

struct data
{
    string name;
    bool bitfield;
    bool onCan1;
    bool isSigned;
    int priority;
    int canID;
    double canMessage;
    struct data *next;
};
// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 18, 2026
// `data` collides with std::data (C++17) under `using namespace std;`,
// so the code refers to the node type through this alias.
using CanNode = struct data;

// incoming can messages
struct CanFrame
{
    int canID;
    double value;
};

class disectCan
{
public:
    disectCan() = default;
    ~disectCan();                                     // frees any nodes still held
    disectCan(const disectCan &) = delete;            // class owns raw pointers,
    disectCan &operator=(const disectCan &) = delete; // so no copying

    void getCanLine(const vector<CanFrame> &messages); // CHANGED (was int line)
    void printMessage();
    void writeCanData(); // NEW

private:
    struct RefRow // NEW
    {
        string name;
        bool bitfield;
        bool onCan1;
        bool isSigned;
        int priority;
    };

    void getTranslation(); // loads Can_ID_Reference.txt into
    void setBitfield(bool value);
    void setOnCan1(bool value);
    void setIsSigned(bool value);
    void setPriority(int value);
    void setCanID(int value);
    void setCanMessage(double value);

    void insertByPriority(CanNode *node);
    void freeCanData();

    CanNode *canData = nullptr;           // head of the list
    unordered_map<int, RefRow> reference; // canID -> reference info
};

#endif