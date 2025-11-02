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

#include "DataBaseException.h"

using namespace SaoFU;
using namespace std;

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
        if (this != &other) { swap(h_, other.h_); }
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
bool DatabaseAccess::connect(const wstring& connection_str) {
    if (is_connected) {
        wcerr << L"Already connected to database.\n";
        return false;
    }

    SQLWCHAR conn_str[1024];
    SQLSMALLINT len;

    SQLRETURN ret = SQLDriverConnect(h_dbc, NULL, (SQLWCHAR*)connection_str.c_str(), SQL_NTS,
        conn_str, sizeof(conn_str) / sizeof(SQLWCHAR), &len, SQL_DRIVER_COMPLETE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        is_connected = true;
        wcout << L"Connected to database successfully!\n";
        return true;
    }
    else {
        throw DataBaseException(L"Failed to connect to database.\n", h_dbc, SQL_HANDLE_DBC);
    }
}


DatabaseAccess&& DatabaseAccess::connect(const wstring& server, const wstring& uid, const wstring& pwd) {
    if (is_connected){
        return move(*this);
    }

    SQLWCHAR cs[1024];
    swprintf(cs, L"DRIVER={SQL Server};SERVER=%s;UID=%s;PWD=%s;", server.c_str(), uid.c_str(), pwd.c_str());

    SQLWCHAR conn_str[1024];
    SQLSMALLINT len;

    SQLRETURN ret = SQLDriverConnect(h_dbc, NULL, cs, SQL_NTS,
        conn_str, sizeof(conn_str) / sizeof(SQLWCHAR), &len, SQL_DRIVER_COMPLETE);

    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        is_connected = true;
        wcout << L"Connected to database successfully!\n";
    }
    else {
        throw DataBaseException(L"Failed to connect to database.\n", h_dbc, SQL_HANDLE_DBC);
    }

    return move(*this);
}


DatabaseAccess&& DatabaseAccess::set_database(const wstring& database) {
    wstring useDb = L"USE " + database;
    StmtHandle h_stmt(h_dbc);
    SQLExecDirectW(h_stmt, (SQLWCHAR*)useDb.c_str(), SQL_NTS);
    return move(*this);
}

SaoFU::DataTable DatabaseAccess::procedure(const std::wstring& procedure_name) const {
    std::wostringstream out;
    out << L"\n" << "EXEC " << procedure_name << " ";
    auto exec_query = out.str();
    return command(exec_query);
}


// **執行 SQL 指令**
DataTable DatabaseAccess::command(const wstring& query, const initializer_list<wstring>& params) const {
    StmtHandle h_stmt(h_dbc);

    // 1) Prepare：使用 '?' 位置參數
    if (!SQL_SUCCEEDED(SQLPrepareW(h_stmt, (SQLWCHAR*)query.c_str(), SQL_NTS))) {
        throw DataBaseException(L"SQLPrepareW failed", h_stmt, SQL_HANDLE_STMT);
    }

    // 2) 綁定所有參數
    vector<SQLLEN> ind(params.size(), SQL_NTS);
    SQLUSMALLINT i = 0;
    for (const auto& row : params) {
        SQLRETURN ret = SQLBindParameter(
            h_stmt,
            (SQLUSMALLINT)(i + 1),
            SQL_PARAM_INPUT,
            SQL_C_WCHAR,
            SQL_WVARCHAR,
            (SQLULEN)max<size_t>(row.size(), 1),
            0,
            (SQLPOINTER)row.c_str(),
            0,
            &ind[i]
        );

        if (!SQL_SUCCEEDED(ret)) {
            throw DataBaseException(L"SQLBindParameter failed", h_stmt, SQL_HANDLE_STMT);
        }
        ++i; // 綁定完再遞增，避免 off-by-one
    }

    // 3) 執行
    if (!SQL_SUCCEEDED(SQLExecute(h_stmt))) {
        throw DataBaseException(L"SQLExecute failed", h_stmt, SQL_HANDLE_STMT);
    }

    vector<ColumnMeta> col_meta;
    DataTable table;

    SQLSMALLINT col_count = 0;
    SQLNumResultCols(h_stmt, &col_count);

    if (col_count > 0) {
        // 欄位描述
        for (SQLUSMALLINT col = 1; col <= col_count; ++col) {
            SQLWCHAR col_name[128] = {};
            ColumnMeta meta{};
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

            meta.name = wstring(col_name);
            col_meta.push_back(meta);
        }

        SQLRETURN frc;
        while ((frc = SQLFetch(h_stmt)) != SQL_NO_DATA) {
            if (!SQL_SUCCEEDED(frc) && frc != SQL_SUCCESS_WITH_INFO) {
                break;
            }

            DataRow row;
            for (SQLUSMALLINT col = 1; col <= col_count; ++col) {
                SQLLEN len = 0;
                vector<BYTE> buf(128, '\0');

                const ColumnMeta& meta = col_meta[col - 1];
                SQLSMALLINT ctype = sql_to_ctype(meta.data_type);
                SQLRETURN rc = SQLGetData(h_stmt, col, ctype, buf.data(), (SQLLEN)buf.size(), &len);
                DataCell cell;

                bool is_success = (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
                if (is_success && len == SQL_NULL_DATA) {
                    cell = DataCell({}, true, meta);
                    row.emplace(meta.name, move(cell));
                    continue;
                }

                if (is_success && len != SQL_NO_TOTAL && len > (SQLLEN)buf.size()) {
                    buf.resize((size_t)len);
                    rc = SQLGetData(h_stmt, col, ctype, buf.data(), (SQLLEN)buf.size(), &len);
                }

                if (len >= 0 && len <= (SQLLEN)buf.size()) {
                    cell = DataCell(vector<BYTE>(buf.begin(), buf.begin() + len), false, meta);
                }
                else {
                    cell = DataCell(move(buf), false, meta);
                }

                row.emplace(meta.name, move(cell));
            }
            table.emplace_back(move(row));
        }
    }

    return table;
}


// **斷開連接**
void DatabaseAccess::disconnect() {
    if (is_connected) {
        SQLDisconnect(h_dbc);
        is_connected = false;
        wcout << L"Disconnected from database.\n";
    }
}
