#include "DataTable.h"

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <iostream>
#include <windows.h>
#include <sqlext.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <unordered_map>

using namespace SaoFU;
using namespace std;

SQLSMALLINT SaoFU::sql_to_ctype(SQLSMALLINT sql_type) {
    static const unordered_map<SQLSMALLINT, SQLSMALLINT> sql_to_ctype_map = {
        // 文字
        {SQL_CHAR,          SQL_C_CHAR},
        {SQL_VARCHAR,       SQL_C_CHAR},
        {SQL_LONGVARCHAR,   SQL_C_CHAR},
        {SQL_WCHAR,         SQL_C_WCHAR},
        {SQL_WVARCHAR,      SQL_C_WCHAR},
        {SQL_WLONGVARCHAR,  SQL_C_WCHAR},

        // 數值
        {SQL_TINYINT,       SQL_C_UTINYINT},
        {SQL_SMALLINT,      SQL_C_SSHORT},
        {SQL_INTEGER,       SQL_C_SLONG},
        {SQL_BIGINT,        SQL_C_SBIGINT},
        {SQL_REAL,          SQL_C_FLOAT},
        {SQL_FLOAT,         SQL_C_DOUBLE},
        {SQL_DOUBLE,        SQL_C_DOUBLE},
        {SQL_BIT,           SQL_C_BIT},

        // 日期時間
        {SQL_TYPE_DATE,     SQL_C_TYPE_DATE},
        {SQL_TYPE_TIME,     SQL_C_TYPE_TIME},
        {SQL_TYPE_TIMESTAMP,SQL_C_TYPE_TIMESTAMP},

        // Binary / rowversion
        {SQL_BINARY,        SQL_C_BINARY},
        {SQL_VARBINARY,     SQL_C_BINARY},
        {SQL_LONGVARBINARY, SQL_C_BINARY},
        {98,                SQL_C_BINARY} // SQL_ROWVERSION
    };

    auto it = sql_to_ctype_map.find(sql_type);
    if (it != sql_to_ctype_map.end()) {
        return it->second;
    }
    return SQL_C_BINARY; // fallback
}

DataCell::DataCell() = default;

DataCell::DataCell(vector<BYTE>&& buf, bool is_null, const ColumnMeta& m): buffer(move(buf)), null_flag(is_null), meta(m) {
}

template <typename T>
T DataCell::get() const {
    if (null_flag || buffer.size() < sizeof(T)) {
        throw runtime_error("Invalid or NULL data for requested type");
    }
    return *(const T*)buffer.data();
}

wstring DataCell::to_string() const {
    if (null_flag || buffer.empty()) return L"(NULL)";

    switch (meta.data_type) {
    case SQL_WCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
        return get<wstring>();
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR: {
        string ascii_str((const char*)buffer.data(), buffer.size());
        ascii_str.append(1, '\0');
        return wstring(ascii_str.begin(), ascii_str.end());
    }

    case SQL_TYPE_DATE: {
        const DATE_STRUCT* d = (const DATE_STRUCT*)buffer.data();
        SYSTEMTIME st{}; st.wYear = d->year; st.wMonth = d->month; st.wDay = d->day;
        wchar_t buf[64];
        if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, buf, 64, nullptr)) return buf;
        return L"(Invalid DATE)";
    }

    case SQL_TYPE_TIME: {
        const TIME_STRUCT* t = (const TIME_STRUCT*)buffer.data();
        SYSTEMTIME st{}; st.wHour = t->hour; st.wMinute = t->minute; st.wSecond = t->second;
        wchar_t buf[64];
        if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, buf, 64)) return buf;
        return L"(Invalid TIME)";
    }

    case SQL_TYPE_TIMESTAMP: {
        const TIMESTAMP_STRUCT* ts = (const TIMESTAMP_STRUCT*)buffer.data();
        SYSTEMTIME st{};
        st.wYear = ts->year; st.wMonth = ts->month; st.wDay = ts->day;
        st.wHour = ts->hour; st.wMinute = ts->minute; st.wSecond = ts->second;
        st.wMilliseconds = (WORD)(ts->fraction / 1000000u);
        wchar_t date_buf[64], time_buf[64];
        if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, date_buf, 64, nullptr) &&
            GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, time_buf, 64)) {
            wstring result = date_buf; result += L" "; result += time_buf; return result;
        }
        return L"(Invalid TIMESTAMP)";
    }

    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
    case 98: {
        wstringstream ss; ss << L"0x" << hex << setfill(L'0');
        for (BYTE b : buffer) ss << setw(2) << static_cast<int>(b);
        return ss.str();
    }

    case SQL_INTEGER:   return to_wstring(get<int>());
    case SQL_SMALLINT:  return to_wstring(get<short>());
    case SQL_BIGINT:    return to_wstring(get<long long>());
    case SQL_DOUBLE:
    case SQL_FLOAT:
    case SQL_REAL:      return to_wstring(get<double>());
    case SQL_BIT:       return get<unsigned char>() ? L"true" : L"false";

    default:
        return L"(Unsupported Type)";
    }
}

bool DataCell::is_null() const {
    return null_flag;
}