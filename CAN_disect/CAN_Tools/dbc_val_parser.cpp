#include "dbc_val_parser.h"

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace std;

bool parseValLine(const string &line, DbcValueLocation &valLoc)
{
    valLoc = DbcValueLocation{}; // fresh result on every call, even on failure paths

    istringstream iss(line);
    string tag;
    iss >> tag;
    if (tag != "VAL_")
        return false;

    if (!(iss >> valLoc.ID >> valLoc.name)) // rejects env-var VAL_ lines (no numeric ID)
        return false;

    vector<string> words;
    string token;
    while (iss >> token)
        words.push_back(token);

    bool expectValue = true; // true = next token is a number
    bool inLabel = false;    // true while inside a quoted label that spans tokens
    bool firstPiece = true;
    string label;

    for (const auto &raw : words)
    {
        string word = raw;

        if (!inLabel && word == ";")
            break;

        if (expectValue)
        {
            valLoc.signal.push_back(stoll(word)); // may throw on a non-numeric token
            expectValue = false;
            continue;
        }

        // expecting label text
        if (!inLabel)
        {
            if (word.front() != '"')
                return false; // improper form
            word.erase(0, 1); // drop opening quote
        }

        // a '"' followed by ';' ends the whole statement
        bool endsStatement = false;
        if (word.size() >= 2 && word.back() == ';' && word[word.size() - 2] == '"')
        {
            word.pop_back();
            endsStatement = true;
        }

        bool closes = !word.empty() && word.back() == '"';
        if (closes)
            word.pop_back();

        if (firstPiece)
            label = word;
        else
            label += " " + word;
        firstPiece = false;

        if (closes)
        {
            valLoc.messages.push_back(label);
            inLabel = false;
            firstPiece = true;
            expectValue = true;
            if (endsStatement)
                break;
        }
        else
        {
            inLabel = true; // label continues in the next token
        }
    }

    if (inLabel)
        return false; // ran out of tokens before the closing quote

    return true;
}

vector<DbcValueLocation> readValRecords(const string &path, vector<int> &keys)
{
    vector<DbcValueLocation> records;
    ifstream in(path);
    string line;

    auto endsWithSemicolon = [](const string &s)
    {
        size_t last = s.find_last_not_of(" \t\r\n");
        return last != string::npos && s[last] == ';';
    };

    while (getline(in, line))
    {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == string::npos || line.compare(start, 4, "VAL_") != 0)
            continue;

        string stmt = line.substr(start);
        while (!endsWithSemicolon(stmt) && getline(in, line)) // join wrapped statements
            stmt += " " + line;

        DbcValueLocation rec;
        try
        {
            if (!parseValLine(stmt, rec))
                continue; // env-var / malformed statement, skipped
        }
        catch (const exception &)
        {
            continue; // a non-numeric value token, skip this statement
        }

        keys.push_back(static_cast<int>(rec.ID & 0x1FFFFFFFu)); // key, made right here
        records.push_back(move(rec));                           // keys[i] belongs to records[i]
    }
    return records;
}

MsgHashTable buildTable(const vector<DbcValueLocation> &records, const vector<int> &keys)
{
    vector<int> distinct = keys;
    sort(distinct.begin(), distinct.end());
    distinct.erase(unique(distinct.begin(), distinct.end()), distinct.end());

    MsgHashTable table(nextPow2(static_cast<size_t>(distinct.size() / 0.75) + 1));

    for (size_t i = 0; i < records.size(); ++i)
    {
        const DbcValueLocation &rec = records[i];
        MessageLabels &msg = table.getOrCreate(keys[i]);

        SignalLabels *sig = nullptr; // walk the chain of signals on this message
        for (auto &s : msg.signals)
            if (s.signalName == rec.name)
            {
                sig = &s;
                break;
            }
        if (sig == nullptr)
        {
            msg.signals.push_back({rec.name, {}});
            sig = &msg.signals.back();
        }

        for (size_t j = 0; j < rec.signal.size() && j < rec.messages.size(); ++j)
            sig->labels[rec.signal[j]] = rec.messages[j];
    }
    return table;
}
