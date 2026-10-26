#include "String_Write.h"

void StringWriter::setFileName(std::string fileString)
{
    fileName = fileString;
}

std::vector<std::string> separateStringHelper(std::string message)
{
    std::vector<std::string> returnStrings;
    std::string parseString = "";
    for (char currentChar : message)
    {
        if (currentChar == '=')
        {
            returnStrings.push_back(parseString);
            parseString = "";
            continue;
        }
        parseString += currentChar;
    }
    returnStrings.push_back(parseString);
    return returnStrings;
}

void StringWriter::separateString(std::string rawString)
{
    splitString.name.clear();
    splitString.message.clear();

    std::vector<std::string> messages;
    std::string currString = "";
    int wordIndex = 0;
    bool consecutiveSapce = false;

    auto storeField = [&]()
    {
        switch (wordIndex)
        {
        case 0: // sets date & time
            splitString.date = currString;
            break;
        case 1: // sets ID
            splitString.ID = currString;
            break;
        case 2: // sets location
            splitString.location = currString;
            break;
        default: // sets list of messages and message names
            messages = separateStringHelper(currString);
            splitString.name.push_back(messages[0]);
            splitString.message.push_back(messages[1]);
            break;
        }
        currString = "";
    };

    for (char currentChar : rawString)
    {
        if (currentChar == ' ' && consecutiveSapce)
        { // two spaces = end of field
            storeField();
            wordIndex++;
            consecutiveSapce = false;
            continue;
        }
        else if (currentChar == ' ' && !consecutiveSapce)
        {
            consecutiveSapce = true;
            continue;
        }
        else
        {
            if (consecutiveSapce)
                currString += ' '; // lone space stays inside the field
            consecutiveSapce = false;
        }
        currString += currentChar;
    }
    storeField(); // last field has no trailing double space
}

bool StringWriter::writeFile(std::string rawString)
{
    separateString(rawString);
    std::stringstream ss;

    // Core attributes: date time ID Location
    // (Assuming date variable contains both date & time or matches your exact string layout)

    ss << splitString.date << "|"
       << splitString.ID << "|"
       << splitString.location;

    // Dynamic payload attributes: message_name=message
    // Loop through the parallel vectors safely
    size_t pairCount = std::min(splitString.name.size(), splitString.message.size());
    for (size_t i = 0; i < pairCount; ++i)
    {
        ss << "|" << splitString.name[i] << "|" << splitString.message[i];
    }

    // Append a newline character at the end of the log line
    ss << "\n";
    std::string serializedLine = ss.str();

    // 2. FatFS File Operations
    FATFS fs;
    FIL file;
    UINT bytesWritten = 0;
    FRESULT res;

    // Mount the logical drive
    res = f_mount(&fs, "", 1);
    if (res != FR_OK)
    {
        return false;
    }

    // Open file in append mode to protect existing records
    res = f_open(&file, "text_files/CAN_Write.can", FA_WRITE | FA_OPEN_APPEND);
    if (res == FR_OK)
    {
        // Write the raw char buffer extracted from our C++ string
        res = f_write(&file, serializedLine.c_str(), serializedLine.length(), &bytesWritten);

        // Close the file to flush metadata and content safely to disk
        f_close(&file);
    }

    // Unmount drive
    f_mount(NULL, "", 0);

    // Return true only if file operation completed normally and wrote all bytes
    return (res == FR_OK && bytesWritten == serializedLine.length());
}

std::string StringWriter::getFileName()
{
    return fileName;
}
