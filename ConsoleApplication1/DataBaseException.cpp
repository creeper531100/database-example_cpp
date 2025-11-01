#include "DataBaseException.h"

#include <iostream>
#include <sstream>

DataBaseException::DataBaseException(const std::wstring& message, SQLHANDLE hHandle, SQLSMALLINT hType) {
    SQLSMALLINT iRec = 1;
    SQLINTEGER  iError;
    WCHAR       wszMessage[1000] = { 0 };
    WCHAR       wszState[SQL_SQLSTATE_SIZE + 1];

    std::wostringstream oss;

    if (!message.empty()) {
        oss << message << std::endl;
    }

    while (SQLGetDiagRec(hType, hHandle, iRec, wszState, &iError, wszMessage, (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
        if (wcsncmp(wszState, L"01004", 5)) {
            WCHAR tmpMessage[1000];
            swprintf(tmpMessage, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
            oss << tmpMessage;
        }
        ++iRec;
    }

    if (oss.str().empty()) {
        oss << "Unknown ODBC error.";
    }

    this->message = oss.str();
    std::wcerr << this->message << std::endl;
}



const wchar_t* DataBaseException::what() const noexcept {
    return message.c_str();
}
