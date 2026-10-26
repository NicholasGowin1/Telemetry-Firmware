#include "dbc_scan.h"

#include <fstream>
#include <sstream>

std::vector<ScannedMessage> collectMessageIds(const std::string &path)
{
    std::vector<ScannedMessage> out;
    std::ifstream in(path);
    std::string line;

    while (std::getline(in, line))
    {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos || line.compare(start, 3, "BO_") != 0)
            continue;

        // Same "BO_ <id> <name>: <dlc> <sender>" shape as dbc_parser.cpp's
        // parseBoLine - duplicated here on purpose (see dbc_scan.h) since
        // that parser is private to dbc_parser.cpp.
        std::istringstream iss(line.substr(start));
        std::string tag, name, sender;
        unsigned long rawId = 0;
        int dlc = 0;
        iss >> tag >> rawId >> name >> dlc >> sender;
        if (tag != "BO_")
            continue;
        if (!name.empty() && name.back() == ':')
            name.pop_back();

        ScannedMessage m;
        m.id = static_cast<int>(rawId & 0x1FFFFFFFu);
        m.isExtended = (rawId & 0x80000000u) != 0;
        m.name = name;
        m.dlc = dlc;
        out.push_back(std::move(m));
    }
    return out;
}
