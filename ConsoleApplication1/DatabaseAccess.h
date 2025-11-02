// DatabaseAccess.h
#ifndef DATABASE_ACCESS_H
#define DATABASE_ACCESS_H
#define NOMINMAX
#include <iomanip>
#include <windows.h>
#include <sqlext.h> 
#include <string>

#include "DataTable.h"

#include <sstream>

namespace SaoFU {
    template<class T> struct SqlTypeName; // 主模板

    template<> struct SqlTypeName<std::wstring> { static constexpr const wchar_t* value = L"nvarchar(max)"; };
    template<> struct SqlTypeName<const wchar_t*> { static constexpr const wchar_t* value = L"nvarchar(max)"; };
    template<> struct SqlTypeName<wchar_t*> { static constexpr const wchar_t* value = L"nvarchar(max)"; };

    template<> struct SqlTypeName<DATE_STRUCT> { static constexpr const wchar_t* value = L"date"; };
    template<> struct SqlTypeName<TIME_STRUCT> { static constexpr const wchar_t* value = L"time(0)"; };
    template<> struct SqlTypeName<TIMESTAMP_STRUCT> { static constexpr const wchar_t* value = L"datetime2(7)"; };
    template<> struct SqlTypeName<SYSTEMTIME> { static constexpr const wchar_t* value = L"datetime2(7)"; };
    template<> struct SqlTypeName<FILETIME> { static constexpr const wchar_t* value = L"datetime2(7)"; };

    template<> struct SqlTypeName<int8_t> { static constexpr const wchar_t* value = L"tinyint"; };
    template<> struct SqlTypeName<int16_t> { static constexpr const wchar_t* value = L"smallint"; };
    template<> struct SqlTypeName<int32_t> { static constexpr const wchar_t* value = L"int"; };
    template<> struct SqlTypeName<int64_t> { static constexpr const wchar_t* value = L"bigint"; };

    template<> struct SqlTypeName<uint8_t> { static constexpr const wchar_t* value = L"bigint"; };
    template<> struct SqlTypeName<uint16_t> { static constexpr const wchar_t* value = L"bigint"; };
    template<> struct SqlTypeName<uint32_t> { static constexpr const wchar_t* value = L"bigint"; };
    template<> struct SqlTypeName<uint64_t> { static constexpr const wchar_t* value = L"bigint"; };

    template<> struct SqlTypeName<float> { static constexpr const wchar_t* value = L"real"; };
    template<> struct SqlTypeName<double> { static constexpr const wchar_t* value = L"float"; };
    template<> struct SqlTypeName<long double> { static constexpr const wchar_t* value = L"float"; };

    template<> struct SqlTypeName<bool> { static constexpr const wchar_t* value = L"bit"; };
    template<> struct SqlTypeName<std::vector<std::uint8_t>> { static constexpr const wchar_t* value = L"varbinary(max)"; };
    template<> struct SqlTypeName<GUID> { static constexpr const wchar_t* value = L"uniqueidentifier"; };

    struct SQLCMD {
        std::wstring text;
        SQLCMD(const wchar_t* s) : text(s) {}
        SQLCMD(std::wstring s) : text(std::move(s)) {}
    };

    template<> struct SqlTypeName<SQLCMD> { static constexpr const wchar_t* value = L"nvarchar(max)"; };


    template<typename T>
    std::wstring emit_value(const T& v) {
        std::wostringstream os;
        os << v;
        return os.str();
    }

    std::wstring emit_value(const wchar_t* s);
    std::wstring emit_value(wchar_t* s);

    std::wstring emit_value(const std::vector<std::uint8_t>& data);
    std::wstring emit_value(const GUID& g);
    std::wstring emit_value(bool v);
    std::wstring emit_value(const std::wstring& v);
    std::wstring emit_value(const SYSTEMTIME& st);
    std::wstring emit_value(const TIME_STRUCT& ts);
    std::wstring emit_value(const DATE_STRUCT& ts);
    std::wstring emit_value(const TIMESTAMP_STRUCT& ts);
    std::wstring emit_value(const FILETIME& ft);

    std::wstring emit_value(const SQLCMD& s);


    template<class T>
    void parse_arg(std::wostream& os, const std::wstring& name, const T& v) {
        using BareT = typename std::decay<T>::type;
        os << L"DECLARE @" << name << L" " << SqlTypeName<BareT>::value << L" = " << emit_value(v) << L";\n";
    }

    template<class T>
    std::wstring parse_arg(const std::wstring& name, const T& v) {
        std::wostringstream out;
        parse_arg(out, name, v);
        return out.str();
    }

    // ------- 展開工具：用自由函式，避免 this/const 問題 -------
    template<typename Tuple, std::size_t... I>
    void expand_parse_args(std::wostream& os, const std::vector<std::wstring>& names, Tuple&& values, std::index_sequence<I...>) {
        int dummy[] = { 0, ((void)parse_arg(os, names[I], std::get<I>(values)), 0)... };
        (void)dummy;
    }
}


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

    SaoFU::DataTable procedure(const std::wstring& procedure_name) const;

    template<typename... Ts>
    SaoFU::DataTable insert_identity_key(const std::wstring& table_name, const std::wstring param_name, Ts&&... ts) {
        std::vector<std::wstring> tokens;
        std::wistringstream ss(param_name);
        std::wstring tok;
        while (std::getline(ss, tok, L',')) {
            tokens.emplace_back(tok);
        }

        std::wostringstream out;
        auto tuple_args = std::forward_as_tuple(std::forward<Ts>(ts)...);
        SaoFU::expand_parse_args(out, tokens, tuple_args, std::make_index_sequence<sizeof...(Ts)>{});

        out << L"\n" << L"INSERT INTO " << table_name << L"(\n";

        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) {
                out << L",";
            }
            out << tokens[i];
        }

        out << L") VALUES (\n";
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) {
                out << L",";
            }
            out << L'@' << tokens[i];
        }

        out << L");\nSELECT SCOPE_IDENTITY() AS NewIdentityKey;\n";

        auto exec_query = out.str();
        return command(exec_query);
    }

    template<typename... Ts>
    SaoFU::DataTable procedure(const std::wstring& procedure_name, std::wstring param_name, Ts&&... ts) const {
        std::vector<std::wstring> tokens;
        std::wistringstream ss(param_name);
        std::wstring tok;

        while (std::getline(ss, tok, L',')) {
            tokens.emplace_back(tok);
        }

        std::wostringstream out;
        auto tuple_args = std::forward_as_tuple(std::forward<Ts>(ts)...);
        SaoFU::expand_parse_args(out, tokens, tuple_args, std::make_index_sequence<sizeof...(Ts)>{});

        out << L"\n" << "EXEC " << procedure_name << " ";
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) {
                out << L",";
            }
            out << L'@' << tokens[i] << L"=" << L'@' << tokens[i];
        }

        auto exec_query = out.str();
        return command(exec_query);
    }

    SaoFU::DataTable command(const std::wstring& query, const std::initializer_list<std::wstring>& params = {}) const;

    template<typename... Ts>
    SaoFU::DataTable command(const std::wstring& procedure_name, std::wstring param_name, Ts&&... ts) const {
        std::vector<std::wstring> tokens;
        std::wistringstream ss(param_name);
        std::wstring tok;
        while (std::getline(ss, tok, L',')) {
            tokens.emplace_back(tok);
        }

        std::wostringstream out;
        auto tuple_args = std::forward_as_tuple(std::forward<Ts>(ts)...);
        SaoFU::expand_parse_args(out, tokens, tuple_args, std::make_index_sequence<sizeof...(Ts)>{});

        out << L"\n" << procedure_name;
        auto exec_query = out.str();
        return command(exec_query);
    }

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