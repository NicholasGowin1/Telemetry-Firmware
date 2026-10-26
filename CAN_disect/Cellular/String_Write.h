#ifndef STRING_WRITE_H
#define STRING_WRITE_H

#include "fatfs/ff.h"
#include <cstring>
#include <string>
#include <sstream>
#include <vector>

struct splitString // structured as date time  ID  Location  message_name=message  message_name=message...  message_name=message
{
    std::string date,
        ID,
        location;
    std::vector<std::string> name,
        message;
};

class StringWriter
{
public:
    void setFileName(std::string fileString);
    void separateString(std::string rawString);
    bool writeFile(std::string rawString);

    std::string getFileName();

private:
    std::string fileName;
    splitString splitString;
};

#endif // STRING_WRITE_H