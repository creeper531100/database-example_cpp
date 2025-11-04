#include <windows.h>
#include <winevt.h>
#include <iostream>
#include <sddl.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "wevtapi.lib")
#include <string>
#include <vector>

#include "DatabaseAccess.h"

std::wstring formatFileTimeToSQLDateTime(const FILETIME* ft) {
    FILETIME localFt;
    SYSTEMTIME st;

    // 轉成本地時間
    FileTimeToLocalFileTime(ft, &localFt);
    FileTimeToSystemTime(&localFt, &st);

    wchar_t date_buf[64];
    wchar_t time_buf[64];

    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, date_buf, 64, nullptr) &&
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, time_buf, 64)) {

        wchar_t result_buf[128];
        swprintf(result_buf, 128, L"%s %s.%03d", date_buf, time_buf, st.wMilliseconds);
        return result_buf;
    }

    return L"";
}

std::wstring FormatEventMessage(EVT_HANDLE hMetadata, EVT_HANDLE hEvent, DWORD flags) {
    DWORD dwBufferUsed = 0;
    std::vector<WCHAR> messageBuffer;

    if (!EvtFormatMessage(hMetadata, hEvent, 0, 0, NULL, flags, 0, NULL, &dwBufferUsed)) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            messageBuffer.resize(dwBufferUsed);
            if (EvtFormatMessage(hMetadata, hEvent, 0, 0, NULL, flags, dwBufferUsed, messageBuffer.data(), &dwBufferUsed)) {
                return std::wstring(messageBuffer.data());
            }
            else {
                return L"Failed to format message. Error: " + std::to_wstring(GetLastError());
            }
        }
        else {
            LPVOID lpMsgBuf;
            DWORD bufLen = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                GetLastError(),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPWSTR)&lpMsgBuf,
                0, 
                NULL
            );
            return L"Failed to get required buffer size. Error: " + std::wstring((LPWSTR)lpMsgBuf) + L" " + std::to_wstring(GetLastError());
        }
    }
    return L"";
}

// 事件订阅回调函数
DWORD WINAPI EvtSubscribeCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, DatabaseAccess* context, EVT_HANDLE hEvent) {
    if (action == EvtSubscribeActionDeliver) {
        std::wcout << L"\n=== New Event Received ===\n";
        DWORD status = ERROR_SUCCESS;
        EVT_HANDLE hContext = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);
        if (!hContext) {
            return GetLastError();
        }

        DWORD dwBufferUsed = 0;
        DWORD dwPropertyCount = 0;

        // 取得所需的 Buffer 大小
        if (!EvtRender(hContext, hEvent, EvtRenderEventValues, 0, NULL, &dwBufferUsed, &dwPropertyCount)) {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                EvtClose(hContext);
                return GetLastError();
            }
        }

        // 使用 std::vector 來管理 Buffer
        std::vector<BYTE> buffer(dwBufferUsed);
        PEVT_VARIANT pRenderedValues = (PEVT_VARIANT)buffer.data();

        if (!EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferUsed, pRenderedValues, &dwBufferUsed,
            &dwPropertyCount)) {
            EvtClose(hContext);
            return GetLastError();
        }

        // 從事件取得 Publisher 名稱，以開啟 Metadata
        EVT_HANDLE hMetadata = NULL;
        DWORD dwPublisherBufferUsed = 0;
        if (!EvtRender(hContext, hEvent, EvtRenderEventValues, dwBufferUsed, pRenderedValues, &dwPublisherBufferUsed, &dwPropertyCount)) {
            EvtClose(hContext);
            return GetLastError();
        }

        if (pRenderedValues[EvtSystemProviderName].Type == EvtVarTypeString) {
            hMetadata = EvtOpenPublisherMetadata(NULL, pRenderedValues[EvtSystemProviderName].StringVal, NULL, 0, 0);
        }

        std::wstring MessageEvent = FormatEventMessage(hMetadata, hEvent, EvtFormatMessageEvent);
        std::wstring MessageProvider = FormatEventMessage(hMetadata, hEvent, EvtFormatMessageProvider);
        std::wstring MessageKeyword = FormatEventMessage(hMetadata, hEvent, EvtFormatMessageKeyword);
        std::wstring MessageTask = FormatEventMessage(hMetadata, hEvent, EvtFormatMessageTask);

        SYSTEMTIME st;
        ULONGLONG ullTimeStamp = pRenderedValues[EvtSystemTimeCreated].FileTimeVal;
        std::wstring file_time = formatFileTimeToSQLDateTime((FILETIME*)&ullTimeStamp);

        wprintf(L"Provider: %s | EventID: %u  | Time: %s\n",
            pRenderedValues[EvtSystemProviderName].StringVal, //來源
            pRenderedValues[EvtSystemEventID].UInt16Val,      //事件識別碼
            file_time.c_str()                                 //日期和時間
        );

        context->insert_identity_key(
        L"event.dbo.even", 
        L"關鍵字,日期和時間,事件識別碼,來源,工作類別,內容,LogDate",
            MessageKeyword,
            *(FILETIME*)&ullTimeStamp,
            pRenderedValues[EvtSystemEventID].UInt16Val,
            MessageTask,
            MessageKeyword,
            MessageEvent,
            (SaoFU::SQLCMD)L"SYSDATETIME()"
        );


        //sprintf();
        //DA->command();


        if (hMetadata)
            EvtClose(hMetadata);

        EvtClose(hContext);
        return status;
    }
    return 0;
}

int main() {
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    const wchar_t* query = LR"(
        <QueryList>
          <Query Id="0" Path="Application">
            <Select Path="Application">*</Select>
          </Query>
          <Query Id="1" Path="System">
            <Select Path="System">*</Select>
          </Query>
          <Query Id="2" Path="Security">
            <Select Path="Security">*</Select>
          </Query>
        </QueryList>
    )";

    std::unique_ptr<DatabaseAccess> DA = std::make_unique<DatabaseAccess>();
    DA->connect(L"DESKTOP-SO1AP2J", L"sa", L"Ww920626@")
        .set_database(L"event");

    EVT_HANDLE hSubscription = EvtSubscribe(
        NULL,
        NULL,
        NULL,
        query,
        NULL,
        DA.get(),
        (EVT_SUBSCRIBE_CALLBACK)EvtSubscribeCallback,
        EvtSubscribeToFutureEvents
    );
    
    if (!hSubscription) {
        std::wcerr << L"Failed to subscribe to events. Error: " << GetLastError() << std::endl;
        return 1;
    }
    
    std::wcout << L"Listening for events...\nPress Enter to exit.\n";
    std::wcin.get();
    
    EvtClose(hSubscription);
    return 0;
}
