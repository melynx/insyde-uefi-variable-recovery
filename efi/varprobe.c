// SPDX-License-Identifier: GPL-2.0-only

#include <efi.h>
#include <efilib.h>

#define LOG_PATH L"\\uefi-var-probe.log"
#define NAME_BUFFER_BYTES (64 * 1024)
#define LINUX_NAME_BUFFER_BYTES 512
#define LINE_BUFFER_BYTES  2048

typedef struct {
    char data[LINE_BUFFER_BYTES];
    UINTN length;
} LINE;

typedef struct {
    CHAR16 *name;
    EFI_GUID guid;
} PROBE;

static EFI_FILE_HANDLE mRoot;
static EFI_FILE_HANDLE mLog;

static EFI_GUID mGlobalVariableGuid = {
    0x8be4df61, 0x93ca, 0x11d2,
    { 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c }
};

static EFI_GUID mImageSecurityDatabaseGuid = {
    0xd719b2cb, 0x3d3a, 0x4596,
    { 0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f }
};

static UINTN
ascii_length(const char *text)
{
    UINTN length = 0;

    while (text[length] != '\0')
        ++length;

    return length;
}

static void
line_reset(LINE *line)
{
    line->length = 0;
    line->data[0] = '\0';
}

static void
line_append_char(LINE *line, char value)
{
    if (line->length + 1 >= LINE_BUFFER_BYTES)
        return;

    line->data[line->length++] = value;
    line->data[line->length] = '\0';
}

static void
line_append_ascii(LINE *line, const char *text)
{
    while (*text != '\0') {
        line_append_char(line, *text);
        ++text;
    }
}

static void
line_append_utf16(LINE *line, const CHAR16 *text)
{
    while (*text != 0) {
        if (*text >= 0x20 && *text <= 0x7e)
            line_append_char(line, (char)*text);
        else
            line_append_char(line, '?');
        ++text;
    }
}

static void
line_append_unsigned(LINE *line, UINT64 value)
{
    char digits[24];
    UINTN count = 0;

    if (value == 0) {
        line_append_char(line, '0');
        return;
    }

    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (count != 0)
        line_append_char(line, digits[--count]);
}

static void
line_append_two_digit_decimal(LINE *line, UINT8 value)
{
    if (value < 10)
        line_append_char(line, '0');
    line_append_unsigned(line, value);
}

static void
line_append_hex(LINE *line, UINT64 value, UINTN width)
{
    static const char hex[] = "0123456789abcdef";
    INTN shift;

    for (shift = (INTN)(width - 1) * 4; shift >= 0; shift -= 4)
        line_append_char(line, hex[(value >> shift) & 0xf]);
}

static void
line_append_guid(LINE *line, const EFI_GUID *guid)
{
    UINTN index;

    line_append_hex(line, guid->Data1, 8);
    line_append_char(line, '-');
    line_append_hex(line, guid->Data2, 4);
    line_append_char(line, '-');
    line_append_hex(line, guid->Data3, 4);
    line_append_char(line, '-');
    line_append_hex(line, guid->Data4[0], 2);
    line_append_hex(line, guid->Data4[1], 2);
    line_append_char(line, '-');

    for (index = 2; index < 8; ++index)
        line_append_hex(line, guid->Data4[index], 2);
}

static const char *
status_name(EFI_STATUS status)
{
    if (status == EFI_SUCCESS)
        return "EFI_SUCCESS";
    if (status == EFI_NOT_FOUND)
        return "EFI_NOT_FOUND";
    if (status == EFI_BUFFER_TOO_SMALL)
        return "EFI_BUFFER_TOO_SMALL";
    if (status == EFI_INVALID_PARAMETER)
        return "EFI_INVALID_PARAMETER";
    if (status == EFI_DEVICE_ERROR)
        return "EFI_DEVICE_ERROR";
    if (status == EFI_SECURITY_VIOLATION)
        return "EFI_SECURITY_VIOLATION";
    if (status == EFI_ACCESS_DENIED)
        return "EFI_ACCESS_DENIED";
    if (status == EFI_WRITE_PROTECTED)
        return "EFI_WRITE_PROTECTED";
    if (status == EFI_OUT_OF_RESOURCES)
        return "EFI_OUT_OF_RESOURCES";
    if (status == EFI_UNSUPPORTED)
        return "EFI_UNSUPPORTED";
    return "unknown status";
}

static void
line_append_status(LINE *line, EFI_STATUS status)
{
    line_append_ascii(line, status_name(status));
    line_append_ascii(line, " (0x");
    line_append_hex(line, (UINT64)status, sizeof(EFI_STATUS) * 2);
    line_append_char(line, ')');
}

static void
line_append_attributes(LINE *line, UINT32 attributes)
{
    line_append_ascii(line, "0x");
    line_append_hex(line, attributes, 8);
    line_append_ascii(line, " [");

    if (attributes & EFI_VARIABLE_NON_VOLATILE)
        line_append_ascii(line, "NV ");
    if (attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS)
        line_append_ascii(line, "BS ");
    if (attributes & EFI_VARIABLE_RUNTIME_ACCESS)
        line_append_ascii(line, "RT ");
    if (attributes & EFI_VARIABLE_HARDWARE_ERROR_RECORD)
        line_append_ascii(line, "HWERR ");
    if (attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS)
        line_append_ascii(line, "AUTH ");
    if (attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS)
        line_append_ascii(line, "TIME_AUTH ");
    if (attributes & EFI_VARIABLE_APPEND_WRITE)
        line_append_ascii(line, "APPEND ");

    line_append_char(line, ']');
}

static void
write_log_line(const char *text)
{
    EFI_STATUS status;
    UINTN size;
    static const char newline[] = "\r\n";

    if (mLog == NULL)
        return;

    size = ascii_length(text);
    status = uefi_call_wrapper(mLog->Write, 3, mLog, &size, (VOID *)text);
    if (EFI_ERROR(status))
        return;

    size = 2;
    uefi_call_wrapper(mLog->Write, 3, mLog, &size, (VOID *)newline);
}

static void
emit_line(const LINE *line, BOOLEAN show_on_console)
{
    write_log_line(line->data);
    if (show_on_console || mLog == NULL)
        Print(L"%a\r\n", line->data);
}

static void
emit_text(const char *text, BOOLEAN show_on_console)
{
    LINE line;

    line_reset(&line);
    line_append_ascii(&line, text);
    emit_line(&line, show_on_console);
}

static EFI_STATUS
open_log(EFI_HANDLE image_handle)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_FILE_HANDLE old_log = NULL;

    status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        image_handle,
        &LoadedImageProtocol,
        (VOID **)&loaded_image
    );
    if (EFI_ERROR(status))
        return status;

    mRoot = LibOpenRoot(loaded_image->DeviceHandle);
    if (mRoot == NULL)
        return EFI_NOT_FOUND;

    status = uefi_call_wrapper(
        mRoot->Open,
        5,
        mRoot,
        &old_log,
        LOG_PATH,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
        0
    );
    if (!EFI_ERROR(status)) {
        status = uefi_call_wrapper(old_log->Delete, 1, old_log);
        if (EFI_ERROR(status))
            uefi_call_wrapper(old_log->Close, 1, old_log);
    }

    status = uefi_call_wrapper(
        mRoot->Open,
        5,
        mRoot,
        &mLog,
        LOG_PATH,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        0
    );
    if (EFI_ERROR(status))
        mLog = NULL;

    return status;
}

static void
close_log(void)
{
    if (mLog != NULL) {
        uefi_call_wrapper(mLog->Flush, 1, mLog);
        uefi_call_wrapper(mLog->Close, 1, mLog);
        mLog = NULL;
    }

    if (mRoot != NULL) {
        uefi_call_wrapper(mRoot->Close, 1, mRoot);
        mRoot = NULL;
    }
}

static void
log_header(void)
{
    EFI_STATUS status;
    EFI_TIME time;
    LINE line;

    emit_text("UEFI Variable Probe 1.2", TRUE);
    emit_text("Read-only test: this application does not call SetVariable().", TRUE);
    emit_text("", TRUE);

    line_reset(&line);
    line_append_ascii(&line, "Firmware vendor: ");
    line_append_utf16(&line, ST->FirmwareVendor);
    emit_line(&line, FALSE);

    line_reset(&line);
    line_append_ascii(&line, "Firmware revision: 0x");
    line_append_hex(&line, ST->FirmwareRevision, 8);
    emit_line(&line, FALSE);

    line_reset(&line);
    line_append_ascii(&line, "UEFI revision: ");
    line_append_unsigned(&line, ST->Hdr.Revision >> 16);
    line_append_char(&line, '.');
    line_append_unsigned(&line, ST->Hdr.Revision & 0xffff);
    emit_line(&line, FALSE);

    status = uefi_call_wrapper(RT->GetTime, 2, &time, NULL);
    if (!EFI_ERROR(status)) {
        line_reset(&line);
        line_append_ascii(&line, "Firmware time: ");
        line_append_unsigned(&line, time.Year);
        line_append_char(&line, '-');
        line_append_two_digit_decimal(&line, time.Month);
        line_append_char(&line, '-');
        line_append_two_digit_decimal(&line, time.Day);
        line_append_char(&line, ' ');
        line_append_two_digit_decimal(&line, time.Hour);
        line_append_char(&line, ':');
        line_append_two_digit_decimal(&line, time.Minute);
        line_append_char(&line, ':');
        line_append_two_digit_decimal(&line, time.Second);
        emit_line(&line, FALSE);
    }
}

static void
log_query_variable_info(UINT32 attributes, const char *label)
{
    EFI_STATUS status;
    UINT64 maximum_storage = 0;
    UINT64 remaining_storage = 0;
    UINT64 maximum_variable = 0;
    LINE line;

    status = uefi_call_wrapper(
        RT->QueryVariableInfo,
        4,
        attributes,
        &maximum_storage,
        &remaining_storage,
        &maximum_variable
    );

    line_reset(&line);
    line_append_ascii(&line, label);
    line_append_ascii(&line, ": ");
    line_append_status(&line, status);

    if (!EFI_ERROR(status)) {
        line_append_ascii(&line, ", maximum=");
        line_append_unsigned(&line, maximum_storage);
        line_append_ascii(&line, ", remaining=");
        line_append_unsigned(&line, remaining_storage);
        line_append_ascii(&line, ", maximum-variable=");
        line_append_unsigned(&line, maximum_variable);
    }

    emit_line(&line, FALSE);
}

static UINTN
enumerate_variables(UINTN call_buffer_bytes, const char *label)
{
    EFI_STATUS status;
    EFI_STATUS get_status;
    CHAR16 *name = NULL;
    CHAR16 *last_name = NULL;
    EFI_GUID guid = gZeroGuid;
    EFI_GUID last_guid = gZeroGuid;
    UINTN name_size;
    UINTN count = 0;
    UINTN data_size;
    UINT32 attributes;
    BOOLEAN first_runtime_seen = FALSE;
    LINE line;

    status = uefi_call_wrapper(
        BS->AllocatePool,
        3,
        EfiLoaderData,
        NAME_BUFFER_BYTES,
        (VOID **)&name
    );
    if (EFI_ERROR(status)) {
        emit_text("Could not allocate the variable-name buffer.", TRUE);
        return 0;
    }

    status = uefi_call_wrapper(
        BS->AllocatePool,
        3,
        EfiLoaderData,
        NAME_BUFFER_BYTES,
        (VOID **)&last_name
    );
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->FreePool, 1, name);
        emit_text("Could not allocate the previous-name buffer.", TRUE);
        return 0;
    }

    name[0] = 0;
    last_name[0] = 0;

    if (call_buffer_bytes > NAME_BUFFER_BYTES) {
        emit_text("The requested variable-name buffer size is too large.", TRUE);
        uefi_call_wrapper(BS->FreePool, 1, last_name);
        uefi_call_wrapper(BS->FreePool, 1, name);
        return 0;
    }

    emit_text("", FALSE);
    emit_text(label, FALSE);
    Print(L"Enumerating variables with a %d-byte input size...\r\n", call_buffer_bytes);

    for (;;) {
        UINTN last_name_bytes = (StrLen(last_name) + 1) * sizeof(CHAR16);

        CopyMem(name, last_name, last_name_bytes);
        guid = last_guid;
        name_size = call_buffer_bytes;

        status = uefi_call_wrapper(
            RT->GetNextVariableName,
            3,
            &name_size,
            name,
            &guid
        );
        if (EFI_ERROR(status))
            break;

        ++count;
        data_size = 0;
        attributes = 0;
        get_status = uefi_call_wrapper(
            RT->GetVariable,
            5,
            name,
            &guid,
            &attributes,
            &data_size,
            NULL
        );

        line_reset(&line);
        line_append_unsigned(&line, count);
        line_append_ascii(&line, ". ");
        line_append_utf16(&line, name);
        line_append_char(&line, '-');
        line_append_guid(&line, &guid);
        line_append_ascii(&line, " | GetVariable: ");
        line_append_status(&line, get_status);

        if (get_status == EFI_BUFFER_TOO_SMALL || get_status == EFI_SUCCESS) {
            line_append_ascii(&line, " | attrs=");
            line_append_attributes(&line, attributes);
            line_append_ascii(&line, " | bytes=");
            line_append_unsigned(&line, data_size);
        }

        emit_line(&line, FALSE);

        if (!first_runtime_seen &&
            (get_status == EFI_BUFFER_TOO_SMALL || get_status == EFI_SUCCESS) &&
            (attributes & EFI_VARIABLE_RUNTIME_ACCESS)) {
            line_reset(&line);
            line_append_ascii(&line, "First enumerated runtime variable: ");
            line_append_utf16(&line, name);
            line_append_char(&line, '-');
            line_append_guid(&line, &guid);
            line_append_ascii(&line, " | attrs=");
            line_append_attributes(&line, attributes);
            emit_line(&line, TRUE);
            first_runtime_seen = TRUE;
        }

        if ((StrLen(name) + 1) * sizeof(CHAR16) >= NAME_BUFFER_BYTES) {
            status = EFI_BAD_BUFFER_SIZE;
            break;
        }

        CopyMem(last_name, name, (StrLen(name) + 1) * sizeof(CHAR16));
        last_guid = guid;
    }

    line_reset(&line);
    line_append_ascii(&line, label);
    line_append_ascii(&line, " stopped after ");
    line_append_unsigned(&line, count);
    line_append_ascii(&line, " variables with ");
    line_append_status(&line, status);
    emit_line(&line, TRUE);

    line_reset(&line);
    line_append_ascii(&line, "Last successful variable: ");
    if (count == 0) {
        line_append_ascii(&line, "<none>");
    } else {
        line_append_utf16(&line, last_name);
        line_append_char(&line, '-');
        line_append_guid(&line, &last_guid);
    }
    emit_line(&line, FALSE);

    if (status == EFI_BUFFER_TOO_SMALL) {
        line_reset(&line);
        line_append_ascii(&line, "Required next-name buffer size: ");
        line_append_unsigned(&line, name_size);
        line_append_ascii(&line, " bytes");
        emit_line(&line, TRUE);
    }

    uefi_call_wrapper(BS->FreePool, 1, last_name);
    uefi_call_wrapper(BS->FreePool, 1, name);
    return count;
}

static void
probe_variable(const PROBE *probe)
{
    EFI_STATUS status;
    UINTN data_size = 0;
    UINT32 attributes = 0;
    LINE line;

    status = uefi_call_wrapper(
        RT->GetVariable,
        5,
        probe->name,
        (EFI_GUID *)&probe->guid,
        &attributes,
        &data_size,
        NULL
    );

    line_reset(&line);
    line_append_utf16(&line, probe->name);
    line_append_char(&line, '-');
    line_append_guid(&line, &probe->guid);
    line_append_ascii(&line, ": ");
    line_append_status(&line, status);

    if (status == EFI_BUFFER_TOO_SMALL || status == EFI_SUCCESS) {
        line_append_ascii(&line, ", attrs=");
        line_append_attributes(&line, attributes);
        line_append_ascii(&line, ", bytes=");
        line_append_unsigned(&line, data_size);
    }

    emit_line(&line, TRUE);
}

static void
run_direct_probes(void)
{
    static PROBE probes[] = {
        { L"SetupMode",              { 0 } },
        { L"SecureBoot",             { 0 } },
        { L"SignatureSupport",       { 0 } },
        { L"VendorKeys",             { 0 } },
        { L"PK",                     { 0 } },
        { L"KEK",                    { 0 } },
        { L"BootOrder",              { 0 } },
        { L"BootCurrent",            { 0 } },
        { L"OsIndications",          { 0 } },
        { L"OsIndicationsSupported", { 0 } },
        { L"db",                     { 0 } },
        { L"dbx",                    { 0 } }
    };
    UINTN index;

    for (index = 0; index < 10; ++index)
        probes[index].guid = mGlobalVariableGuid;
    probes[10].guid = mImageSecurityDatabaseGuid;
    probes[11].guid = mImageSecurityDatabaseGuid;

    emit_text("", TRUE);
    emit_text("=== Direct GetVariable() probes ===", TRUE);
    for (index = 0; index < sizeof(probes) / sizeof(probes[0]); ++index)
        probe_variable(&probes[index]);
}

EFI_STATUS
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    EFI_STATUS log_status;
    EFI_INPUT_KEY key;
    UINTN linux_count;
    UINTN large_count;
    UINTN index;
    LINE line;

    InitializeLib(image_handle, system_table);
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);

    log_status = open_log(image_handle);
    if (EFI_ERROR(log_status)) {
        Print(L"Could not create %s: %r\r\n", LOG_PATH, log_status);
        Print(L"The complete result will be shown on screen instead.\r\n\r\n");
    }

    log_header();
    emit_text("", FALSE);
    emit_text("=== QueryVariableInfo() ===", FALSE);
    log_query_variable_info(
        EFI_VARIABLE_NON_VOLATILE |
        EFI_VARIABLE_BOOTSERVICE_ACCESS |
        EFI_VARIABLE_RUNTIME_ACCESS,
        "NV+BS+RT"
    );
    log_query_variable_info(
        EFI_VARIABLE_NON_VOLATILE |
        EFI_VARIABLE_BOOTSERVICE_ACCESS |
        EFI_VARIABLE_RUNTIME_ACCESS |
        EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS,
        "NV+BS+RT+TIME_AUTH"
    );

    linux_count = enumerate_variables(
        LINUX_NAME_BUFFER_BYTES,
        "Linux-compatible 512-byte enumeration"
    );
    large_count = enumerate_variables(
        NAME_BUFFER_BYTES,
        "Large-buffer 65536-byte enumeration"
    );

    line_reset(&line);
    line_append_ascii(&line, "Enumeration count comparison: Linux-compatible=");
    line_append_unsigned(&line, linux_count);
    line_append_ascii(&line, ", large-buffer=");
    line_append_unsigned(&line, large_count);
    emit_line(&line, TRUE);

    run_direct_probes();

    emit_text("", TRUE);
    if (mLog != NULL)
        emit_text("Complete results were written to \\uefi-var-probe.log on this USB drive.", TRUE);

    close_log();

    Print(L"Press any key to exit.\r\n");
    uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
    uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
    return EFI_SUCCESS;
}
