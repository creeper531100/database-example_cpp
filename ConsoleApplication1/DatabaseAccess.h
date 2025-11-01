// DatabaseAccess.h
#ifndef DATABASE_ACCESS_H
#define DATABASE_ACCESS_H
#define NOMINMAX
#include <windows.h>
#include <sqlext.h> 
#include <string>

#include "DataTable.h"

class DatabaseAccess {
private:
    SQLHENV h_env;
    SQLHDBC h_dbc;
    bool is_connected;
public:
    DatabaseAccess();
    bool connect(const std::wstring& connection_str);
    DatabaseAccess&& connect(const std::wstring& server, const std::wstring& uid,
                             const std::wstring& pwd);
    DatabaseAccess&& set_database(const std::wstring& database);

    SaoFU::DataTable command(const std::wstring& query, std::wstring param_name, ...);
    SaoFU::DataTable procedure(const std::wstring& procedure_name, std::wstring param_name, ...);
    SaoFU::DataTable command(const std::wstring& query, const std::initializer_list<std::wstring>& params = {});
    void disconnect();
    ~DatabaseAccess();

    DatabaseAccess(const DatabaseAccess&) = delete;
    DatabaseAccess& operator=(const DatabaseAccess&) = delete;
    DatabaseAccess(DatabaseAccess&& other) noexcept;
    DatabaseAccess& operator=(DatabaseAccess&& other) noexcept;

    static DatabaseAccess builder() {
        return DatabaseAccess();
    }
};


#endif // DATABASE_ACCESS_H