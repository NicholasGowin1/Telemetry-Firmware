#include "telemetry.h"

#include <algorithm>
#include <cctype>
// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 18, 2026
// ---------------------------------------------------------------------------
// Small parsing helpers
// ---------------------------------------------------------------------------
static string trim(const string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool toBool(const string &s)
{
    string t = trim(s);
    transform(t.begin(), t.end(), t.begin(),
              [](unsigned char c)
              { return static_cast<char>(tolower(c)); });
    return t == "1" || t == "true" || t == "yes" || t == "y";
}

// Accepts decimal ("419") or hex with a 0x prefix ("0x1A3")
static int toInt(const string &s)
{
    string t = trim(s);
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        return stoi(t, nullptr, 16);
    return stoi(t);
}

disectCan::~disectCan()
{
    freeCanData();
}

void disectCan::freeCanData()
{
    while (canData != nullptr)
    {
        CanNode *node = canData;
        canData = node->next;
        delete node;
    }
}

void disectCan::getTranslation()
{
    reference.clear();

    ifstream file("CAN_ID_Reference.txt");
    if (!file.is_open())
    {
        cout << "Error: could not open file CAN_ID_Reference.txt" << endl;
        return;
    }

    string line;

    getline(file, line); // skips first line because it's the header
    while (getline(file, line, '\n'))
    {
        vector<string> row;
        stringstream ss(line);
        string field;

        while (getline(ss, field, ','))
        {
            row.push_back(field);
        }

        // Update to expect 5 columns
        if (row.size() < 5)
        {
            continue;
        }
        // Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
        // Date: September 18, 2026
        try
        {
            RefRow r;
            // 4. Map columns to match: Name, onBus1, CANID, signed, Priority
            r.name = trim(row[0]);
            r.bitfield = false;        // Not in CSV, default to false
            r.onCan1 = toBool(row[1]); // onBus1

            int canId = toInt(row[2]); // CANID

            r.isSigned = toBool(row[3]); // signed

            // Note: If r.priority in your struct is an int, std::stod will
            // truncate "1.1" to 1. If you need the decimals, change
            // 'priority' in your struct/classes to a double.
            r.priority = std::stod(trim(row[4]));

            reference[canId] = r;
        }
        catch (const exception &e)
        {
            cerr << "Warning: skipping bad reference line: " << line << " (" << e.what() << ")" << endl;
        }
    }
    file.close();
}

void disectCan::insertByPriority(CanNode *node)
{
    if (canData == nullptr || node->priority < canData->priority) // if node is highest priority, or stack is empty set it to top of stack
    {
        node->next = canData;
        canData = node;
        return;
    }

    CanNode *cur = canData;
    while (cur->next != nullptr && cur->next->priority <= node->priority) // finds end of stack or first priority lower than itself
    {
        cur = cur->next;
    }
    node->next = cur->next; // places node on stack
    cur->next = node;
}

void disectCan::getCanLine(const vector<CanFrame> &messages)
{
    if (reference.empty())
    {
        getTranslation();
        if (reference.empty())
        {
            return; // getTranslation already reported why
        }
    }

    for (const CanFrame &msg : messages)
    {
        auto it = reference.find(msg.canID);
        if (it == reference.end())
        {
            cerr << "Warning: CAN ID " << msg.canID
                 << " not found in reference file, skipping" << endl;
            continue;
        }

        CanNode *node = new CanNode;
        node->name = it->second.name;
        node->bitfield = it->second.bitfield;
        node->onCan1 = it->second.onCan1;
        node->isSigned = it->second.isSigned;
        node->priority = it->second.priority;
        node->canID = msg.canID;
        node->canMessage = msg.value;
        node->next = nullptr;

        insertByPriority(node);
    }
}

// ---------------------------------------------------------------------------
// printMessage: prints the list in order without modifying it
// ---------------------------------------------------------------------------
void disectCan::printMessage()
{
    for (const CanNode *n = canData; n != nullptr; n = n->next)
    {
        cout << n->name
             << " | bitfield=" << n->bitfield
             << " | onCan1=" << n->onCan1
             << " | signed=" << n->isSigned
             << " | priority=" << n->priority
             << " | id=" << n->canID
             << " | value=" << n->canMessage
             << '\n';
    }
}

void disectCan::writeCanData()
{
    ofstream out("write.txt", ios::app);
    if (!out.is_open())
    {
        cerr << "Error: could not open file " << "write.txt" << endl;
        return;
    }

    out.precision(10);

    while (canData != nullptr)
    {
        CanNode *node = canData;
        canData = node->next; // advance head before deleting

        out << node->name << ','
            << node->bitfield << ','
            << node->onCan1 << ','
            << node->isSigned << ','
            << node->priority << ','
            << node->canID << ','
            << node->canMessage << '\n';

        delete node;
    }
}

// sets all data members of the current node

void disectCan::setBitfield(bool value)
{
    if (canData)
        canData->bitfield = value;
}
void disectCan::setOnCan1(bool value)
{
    if (canData)
        canData->onCan1 = value;
}
void disectCan::setIsSigned(bool value)
{
    if (canData)
        canData->isSigned = value;
}
void disectCan::setPriority(int value)
{
    if (canData)
        canData->priority = value;
}
void disectCan::setCanID(int value)
{
    if (canData)
        canData->canID = value;
}
void disectCan::setCanMessage(double value)
{
    if (canData)
        canData->canMessage = value;
}