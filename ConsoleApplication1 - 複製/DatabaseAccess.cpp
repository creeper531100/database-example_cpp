#include <windows.h>     // 一定要放最前面！
#include <sqlext.h>      // 不要自己 include sqltypes.h

#include "DatabaseAccess.h"
#include "DataTable.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <iomanip>
#include <sstream>

class StmtHandle {
    SQLHSTMT h_ = nullptr;
public:
    explicit StmtHandle(SQLHDBC dbc) {
        if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &h_) != SQL_SUCCESS) {
            throw DataBaseException(L"SQLAllocHandle(SQL_HANDLE_STMT) failed", dbc, SQL_HANDLE_DBC);
        }
    }
    ~StmtHandle() {
        if (h_) SQLFreeHandle(SQL_HANDLE_STMT, h_);
    }

    StmtHandle(const StmtHandle&) = delete;
    StmtHandle& operator=(const StmtHandle&) = delete;
    StmtHandle(StmtHandle&& other) noexcept : h_(other.h_) { other.h_ = nullptr; }
    StmtHandle& operator=(StmtHandle&& other) noexcept {
        if (this != &other) { std::swap(h_, other.h_); }
        return *this;
    }
    operator SQLHSTMT() const { return h_; }
    SQLHSTMT get() const { return h_; }
};


// 建構函數：順序正確（保留）
DatabaseAccess::DatabaseAccess() : h_env(nullptr), h_dbc(nullptr), is_connected(false) {
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &h_env);
    SQLSetEnvAttr(h_env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0); // 只在這裡設
    SQLAllocHandle(SQL_HANDLE_DBC, h_env, &h_dbc);
}

DatabaseAccess::~DatabaseAccess() {
    disconnect();
    if (h_dbc) {
        SQLFreeHandle(SQL_HANDLE_DBC, h_dbc);
        h_dbc = nullptr;
    }
    if (h_env) {
        SQLFreeHandle(SQL_HANDLE_ENV, h_env);
        h_env = nullptr;
    }
}

DatabaseAccess::DatabaseAccess(DatabaseAccess&& other) noexcept :
    h_env(other.h_env),
    h_dbc(other.h_dbc),
    is_connected(other.is_connected)
{
    other.h_env = nullptr;
    other.h_dbc = nullptr;
    other.is_connected = false;
}

DatabaseAccess& DatabaseAccess::operator=(DatabaseAccess&& other) noexcept {
    if (this != &other) {
        disconnect(); // 確保釋放自身連線

        // 移轉所有 handle
        h_env = other.h_env;
        h_dbc = other.h_dbc;
        is_connected = other.is_connected;

        // 將來源清空，避免重複釋放
        other.h_env = nullptr;
        other.h_dbc = nullptr;
        other.is_connected = false;
    }
    return *this;
}

// **連接資料庫**
bool DatabaseAccess::connect(const std::wstring& connection_str) {
    if (is_connected) {
        std::wcerr << L"Already connected to database.\n";
        return false;
    }

    SQLWCHAR conn_str[1024];
    SQLSMALLINT len;

    SQLRETURN ret = SQLDriverConnect(h_dbc, NULL, (SQLWCHAR*)connection_str.c_str(), SQL_NTS,
        conn_str, sizeof(conn_str) / sizeof(SQLWCHAR), &len, SQL_DRIVER_COMPLETE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        is_connected = true;
        std::wcout << L"Connected to database successfully!\n";
        return true;
    }
    else {
        throw DataBaseException(L"Failed to connect to database.\n", h_dbc, SQL_HANDLE_DBC);
    }
}



bool DatabaseAccess::connect(const std::wstring& server,
    const std::wstring& uid,
    const std::wstring& pwd) {
    if (is_connected) {
        std::wcerr << L"Already connected to database.\n";
        return true; // 或者 return false; 視你的語意
    }

    // 這行拿掉：不能在已分配 HDBC 後再設 ODBC_VERSION
    // SQLSetEnvAttr(h_env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    SQLRETURN ret = SQLConnectW(
        h_dbc,
        (SQLWCHAR*)server.c_str(), SQL_NTS,
        (SQLWCHAR*)uid.c_str(), SQL_NTS,
        (SQLWCHAR*)pwd.c_str(), SQL_NTS
    );

    if (SQL_SUCCEEDED(ret)) {
        is_connected = true;
        std::wcout << L"Connected to database successfully!\n";
        return true;
    }
    else {
        throw DataBaseException(L"Failed to connect to database", h_dbc, SQL_HANDLE_DBC);
    }
}


DatabaseAccess&& DatabaseAccess::set_database(const std::wstring& database) {
    std::wstring useDb = L"USE " + database;
    SQLHSTMT h_stmt;
    SQLAllocHandle(SQL_HANDLE_STMT, h_dbc, &h_stmt);
    SQLExecDirectW(h_stmt, (SQLWCHAR*)useDb.c_str(), SQL_NTS);
    SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);
    return std::move(*this);
}

// **執行 SQL 指令**
SaoFU::DataTable DatabaseAccess::command(const std::wstring& query) {
    std::vector<SaoFU::ColumnMeta> col_meta;
    SaoFU::DataTable table;

    StmtHandle h_stmt(h_dbc);
    if (!SQL_SUCCEEDED(SQLExecDirectW(h_stmt, (SQLWCHAR*)query.c_str(), SQL_NTS))) {
        throw DataBaseException(L"Fail!!!", h_stmt, SQL_HANDLE_STMT);
    }

    SQLSMALLINT col_count = 0;
    SQLNumResultCols(h_stmt, &col_count);

    if (col_count > 0) {
        // 欄位描述
        for (SQLUSMALLINT col = 1; col <= col_count; ++col) {
            SQLWCHAR col_name[128] = {};
            SaoFU::ColumnMeta meta{};
            SQLDescribeColW(
                h_stmt, 
                col, 
                col_name, 
                sizeof(col_name) / sizeof(SQLWCHAR),
                nullptr, 
                &meta.data_type,
                &meta.column_size, 
                &meta.decimal_digits,
                &meta.nullable
            );

            meta.name = std::wstring(col_name);
            col_meta.push_back(meta);
        }

        SQLRETURN frc;
        while ((frc = SQLFetch(h_stmt)) != SQL_NO_DATA) {
            if (!SQL_SUCCEEDED(frc) && frc != SQL_SUCCESS_WITH_INFO) {
                break;
            }

            SaoFU::DataRow row;
            for (SQLUSMALLINT col = 1; col <= col_count; ++col) {
                SQLLEN len = 0;
                std::vector<BYTE> buf(128, '\0');

                const SaoFU::ColumnMeta& meta = col_meta[col - 1];
                SQLSMALLINT ctype = SaoFU::sql_to_ctype(meta.data_type);
                SQLRETURN rc = SQLGetData(h_stmt, col, ctype, buf.data(), (SQLLEN)buf.size(), &len);
                SaoFU::DataCell cell;

                bool is_success = (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
                if (is_success && len == SQL_NULL_DATA) {
                    cell = SaoFU::DataCell({}, true, meta);
                    row.emplace(meta.name, std::move(cell));
                    continue;
                }

                if (is_success && len != SQL_NO_TOTAL && len > (SQLLEN)buf.size()) {
                    buf.resize((size_t)len);
                    rc = SQLGetData(h_stmt, col, ctype, buf.data(), (SQLLEN)buf.size(), &len);
                }

                switch (ctype) {
                case SQL_C_WCHAR:
                    len += sizeof(SQLWCHAR);
                    break;
                case SQL_C_CHAR:
                    len += sizeof(SQLCHAR);
                    break;
                default:
                    break;
                }

                if (len >= 0 && len <= (SQLLEN)buf.size()) {
                    cell = SaoFU::DataCell(std::vector<BYTE>(buf.begin(), buf.begin() + len), false, meta);
                }
                else {
                    cell = SaoFU::DataCell(std::move(buf), false, meta);
                }

                row.emplace(meta.name, std::move(cell));
            }
            table.emplace_back(std::move(row));
        }
    }

    return table;
}

// **斷開連接**
void DatabaseAccess::disconnect() {
    if (is_connected) {
        SQLDisconnect(h_dbc);
        is_connected = false;
        std::wcout << L"Disconnected from database.\n";
    }
}

DataBaseException::DataBaseException(const std::wstring& message, SQLHANDLE hHandle, SQLSMALLINT hType) {
    SQLSMALLINT iRec = 1;
    SQLINTEGER  iError;
    WCHAR       wszMessage[1000] = {0};
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
