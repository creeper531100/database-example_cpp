#ifndef SAOFU_DATA_HPP
#define SAOFU_DATA_HPP

#define NOMINMAX
#include <windows.h>
#include <sqlext.h> 
#include <string>
#include <vector>
#include <minwindef.h>
#include <unordered_map>

namespace SaoFU {
    struct ColumnMeta {
        std::wstring name;
        SQLSMALLINT data_type;
        SQLULEN column_size;
        SQLSMALLINT decimal_digits;
        SQLSMALLINT nullable;
    };

    struct DataCell {
        std::vector<BYTE> buffer;
        bool null_flag = false;
        ColumnMeta meta;

        DataCell();

        DataCell(std::vector<BYTE>&& buf, bool is_null, const ColumnMeta& m);

        template<typename T>
        T get() const;

        std::wstring to_string() const;

        bool is_null() const;
    };

    template<>
    inline std::wstring DataCell::get<std::wstring>() const {
        std::wstring ws((const wchar_t*)buffer.data(), buffer.size() / sizeof(wchar_t));
        ws.append(2, L'\0'); // ¸É¨â­Ó null terminator
        return ws;
    }

    SQLSMALLINT sql_to_ctype(SQLSMALLINT sql_type);

    using DataRow = std::unordered_map<std::wstring, DataCell>;
    using DataTable = std::vector<DataRow>;
}



#endif
