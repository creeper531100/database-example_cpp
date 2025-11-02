#pragma once
// DatabaseAccess.h
#ifndef DATABASE_EXCEPTION_H
#define DATABASE_EXCEPTION_H
#define NOMINMAX
#include <windows.h>
#include <sqlext.h> 
#include <string>

namespace SaoFU {
    class DataBaseException {
        std::wstring message;
    public:
        DataBaseException(const std::wstring& message, SQLHANDLE hHandle, SQLSMALLINT hType);
        const wchar_t* what() const noexcept;
    };
}

#endif
