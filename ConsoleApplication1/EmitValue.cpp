#include "DatabaseAccess.h"
#include "Windows.h"

std::wstring SaoFU::emit_value(const wchar_t* s) {
    return emit_value(std::wstring(s));
}

std::wstring SaoFU::emit_value(wchar_t* s) {
    return emit_value(static_cast<const wchar_t*>(s));
}

std::wstring SaoFU::emit_value(const std::vector<std::uint8_t>& data) {
    std::wostringstream os;
    os << L"0x" << std::uppercase << std::hex << std::setfill(L'0');
    for (auto b : data) {
        os << std::setw(2) << b;
    }
    return os.str();
}

std::wstring SaoFU::emit_value(const GUID& g) {
    std::wostringstream os;
    os << L"'"                       // SQL 字面值用單引號（不加 N）
        << std::uppercase << std::hex << std::setfill(L'0')
        << std::setw(8) << g.Data1 << L"-"
        << std::setw(4) << g.Data2 << L"-"
        << std::setw(4) << g.Data3 << L"-"
        << std::setw(2) << static_cast<unsigned>(g.Data4[0])
        << std::setw(2) << static_cast<unsigned>(g.Data4[1]) << L"-"
        << std::setw(2) << static_cast<unsigned>(g.Data4[2])
        << std::setw(2) << static_cast<unsigned>(g.Data4[3])
        << std::setw(2) << static_cast<unsigned>(g.Data4[4])
        << std::setw(2) << static_cast<unsigned>(g.Data4[5])
        << std::setw(2) << static_cast<unsigned>(g.Data4[6])
        << std::setw(2) << static_cast<unsigned>(g.Data4[7])
        << L"'";
    return os.str();
}

std::wstring SaoFU::emit_value(bool v) {
    return v ? L"true" : L"false";
}

std::wstring SaoFU::emit_value(const std::wstring& v) {
    std::wostringstream os;
    std::wstring out;
    out.reserve(v.size());
    for (wchar_t ch : v) {
        out.push_back(ch); if (ch == L'\'') out.push_back(L'\'');
    }
    os << L"N'" << out << L"'";
    return os.str();
}

std::wstring SaoFU::emit_value(const SYSTEMTIME& st) {
    std::wostringstream os;

    wchar_t date_buf[64] = { 0 };
    wchar_t time_fmt[64] = { 0 };

    GetDateFormatEx(LOCALE_NAME_INVARIANT, 0, &st, nullptr, date_buf, 64, nullptr);

    wchar_t time_buf[64] = { 0 };
    if (GetTimeFormatEx(LOCALE_NAME_INVARIANT, 0, &st, nullptr, time_buf, 64)) {
        swprintf(time_fmt, 128, L"%s.%03d", time_buf, st.wMilliseconds);
    }

    os << L"N'" << date_buf << L" " << time_fmt << L"'";
    return os.str();
}

std::wstring SaoFU::emit_value(const TIME_STRUCT& ts) {
    SYSTEMTIME st = { 0 };
    st.wHour = ts.hour;
    st.wMinute = ts.minute;
    st.wSecond = ts.second;

    return emit_value(st);
}

std::wstring SaoFU::emit_value(const DATE_STRUCT& ts) {
    SYSTEMTIME st = { 0 };
    st.wYear = ts.year;
    st.wMonth = ts.month;
    st.wDay = ts.day;

    return emit_value(st);
}

std::wstring SaoFU::emit_value(const TIMESTAMP_STRUCT& ts) {
    SYSTEMTIME st = { 0 };
    st.wYear = ts.year;
    st.wMonth = ts.month;
    st.wDay = ts.day;
    st.wHour = ts.hour;
    st.wMinute = ts.minute;
    st.wSecond = ts.second;
    st.wMilliseconds = (WORD)(ts.fraction / 1000000u);

    return emit_value(st);
}

std::wstring SaoFU::emit_value(const FILETIME& ft) {
    FILETIME localFt;
    SYSTEMTIME st;

    // 轉成本地時間
    FileTimeToLocalFileTime(&ft, &localFt);
    FileTimeToSystemTime(&localFt, &st);

    return emit_value(st);
}

std::wstring SaoFU::emit_value(const SQLCMD& s) {
    std::wostringstream os;
    os << s.text;
    return os.str();
}
