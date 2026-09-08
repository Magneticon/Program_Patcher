// pp.cpp
// Native PE version patcher.
// Patches PE32 (x86) and PE32+ (x64) executable images.
//
// Syntax:
//   PP file.exe
//       Show PE architecture and current OS/subsystem versions.
//
//   PP file.exe 3.50
//       Patch both OperatingSystemVersion and SubsystemVersion.
//       x86 minimum: 3.50
//       x64 minimum: 5.02
//
// Before patching, an unsigned executable is copied to file.exe.bak.
// If that backup already exists, PP refuses to overwrite it.

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0400
#include <windows.h>

#ifndef IMAGE_FILE_MACHINE_AMD64
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#endif

#ifndef IMAGE_DIRECTORY_ENTRY_SECURITY
#define IMAGE_DIRECTORY_ENTRY_SECURITY 4
#endif

static HANDLE gHeap = 0;
static BYTE gChecksumBuffer[4096];

static SIZE_T WLen(const WCHAR* s)
{
    SIZE_T n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static void WCopy(WCHAR* dst, SIZE_T cap, const WCHAR* src)
{
    SIZE_T i = 0;
    if (!dst || !cap) return;
    if (src) {
        while (src[i] && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static void WCat(WCHAR* dst, SIZE_T cap, const WCHAR* src)
{
    SIZE_T n = WLen(dst);
    if (n >= cap) return;
    WCopy(dst + n, cap - n, src);
}

static void Out(const WCHAR* s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0, written = 0;

    if (!s || h == INVALID_HANDLE_VALUE)
        return;

    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, s, (DWORD)WLen(s), &written, 0);
    } else {
        int need = WideCharToMultiByte(CP_ACP, 0, s, -1, 0, 0, 0, 0);
        if (need > 1) {
            char* b = (char*)HeapAlloc(gHeap, 0, (SIZE_T)need);
            if (b) {
                int got = WideCharToMultiByte(CP_ACP, 0, s, -1,
                                              b, need, 0, 0);
                if (got > 1)
                    WriteFile(h, b, (DWORD)(got - 1), &written, 0);
                HeapFree(gHeap, 0, b);
            }
        }
    }
}

static void OutLn(const WCHAR* s)
{
    Out(s);
    Out(L"\r\n");
}

static void Err(const WCHAR* s)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0, written = 0;

    if (!s || h == INVALID_HANDLE_VALUE)
        return;

    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, s, (DWORD)WLen(s), &written, 0);
    } else {
        int need = WideCharToMultiByte(CP_ACP, 0, s, -1, 0, 0, 0, 0);
        if (need > 1) {
            char* b = (char*)HeapAlloc(gHeap, 0, (SIZE_T)need);
            if (b) {
                int got = WideCharToMultiByte(CP_ACP, 0, s, -1,
                                              b, need, 0, 0);
                if (got > 1)
                    WriteFile(h, b, (DWORD)(got - 1), &written, 0);
                HeapFree(gHeap, 0, b);
            }
        }
    }
}

static void ErrLn(const WCHAR* s)
{
    Err(s);
    Err(L"\r\n");
}

static void UIntToDec(DWORD value, WCHAR* out, SIZE_T cap)
{
    WCHAR tmp[16];
    SIZE_T n = 0, i = 0;

    if (!out || !cap) return;

    if (!value) {
        WCopy(out, cap, L"0");
        return;
    }

    while (value && n < 15) {
        tmp[n++] = (WCHAR)(L'0' + (value % 10));
        value /= 10;
    }

    while (n && i + 1 < cap)
        out[i++] = tmp[--n];

    out[i] = 0;
}

static void PrintVersionPair(WORD major, WORD minor)
{
    WCHAR a[16], b[16];

    UIntToDec((DWORD)major, a, sizeof(a) / sizeof(a[0]));
    UIntToDec((DWORD)minor, b, sizeof(b) / sizeof(b[0]));

    Out(a);
    Out(L".");
    if (minor < 10)
        Out(L"0");
    Out(b);
}

static void PrintUsage()
{
    OutLn(L"PP - Program Patcher v1.00");
    OutLn(L"");
    OutLn(L"Usage:");
    OutLn(L"  PP file.exe");
    OutLn(L"      patch automatically:");
    OutLn(L"        PE32  -> 3.50");
    OutLn(L"        PE32+ -> 5.02");
    OutLn(L"");
    OutLn(L"  PP file.exe major.minor");
    OutLn(L"      patch to a custom version");
    OutLn(L"");
    OutLn(L"  PP file.exe s");
    OutLn(L"      inspect only; do not modify the file");
    OutLn(L"");
    OutLn(L"PP patches both OperatingSystemVersion and SubsystemVersion.");
    OutLn(L"A .bak backup is created before patching.");
    OutLn(L"");
}

static BOOL ParseVersion(const WCHAR* s, WORD* major, WORD* minor)
{
    DWORD a = 0, b = 0;
    SIZE_T i = 0;
    BOOL dot = FALSE;
    BOOL haveA = FALSE;
    BOOL haveB = FALSE;

    if (!s || !*s || !major || !minor)
        return FALSE;

    while (s[i]) {
        WCHAR c = s[i++];

        if (c == L'.') {
            if (dot || !haveA)
                return FALSE;
            dot = TRUE;
            continue;
        }

        if (c < L'0' || c > L'9')
            return FALSE;

        if (!dot) {
            haveA = TRUE;
            if (a > 6553)
                return FALSE;
            a = a * 10 + (DWORD)(c - L'0');
            if (a > 65535)
                return FALSE;
        } else {
            haveB = TRUE;
            if (b > 6553)
                return FALSE;
            b = b * 10 + (DWORD)(c - L'0');
            if (b > 65535)
                return FALSE;
        }
    }

    if (!haveA)
        return FALSE;

    if (!dot)
        b = 0;
    else if (!haveB)
        return FALSE;

    *major = (WORD)a;
    *minor = (WORD)b;
    return TRUE;
}

static int ParseCommandLine(WCHAR*** outArgv, WCHAR** outStorage)
{
    WCHAR* raw = GetCommandLineW();
    SIZE_T len = WLen(raw);
    WCHAR* storage;
    WCHAR** argv;
    WCHAR* src;
    WCHAR* dst;
    int argc = 0;

    storage = (WCHAR*)HeapAlloc(gHeap, HEAP_ZERO_MEMORY,
                                (len + 2) * sizeof(WCHAR));
    argv = (WCHAR**)HeapAlloc(gHeap, HEAP_ZERO_MEMORY,
                              8 * sizeof(WCHAR*));

    if (!storage || !argv) {
        if (storage) HeapFree(gHeap, 0, storage);
        if (argv) HeapFree(gHeap, 0, argv);
        return 0;
    }

    src = raw;
    dst = storage;

    while (*src && argc < 7) {
        WCHAR quote = 0;

        while (*src == L' ' || *src == L'\t')
            ++src;

        if (!*src)
            break;

        argv[argc++] = dst;

        if (*src == L'"' || *src == L'\'')
            quote = *src++;

        while (*src) {
            if (quote) {
                if (*src == quote) {
                    ++src;
                    break;
                }
            } else if (*src == L' ' || *src == L'\t') {
                break;
            }

            *dst++ = *src++;
        }

        *dst++ = 0;

        while (*src == L' ' || *src == L'\t')
            ++src;
    }

    *outArgv = argv;
    *outStorage = storage;
    return argc;
}

static BOOL ReadExact(HANDLE h, void* buffer, DWORD bytes)
{
    DWORD got = 0;
    return ReadFile(h, buffer, bytes, &got, 0) && got == bytes;
}

static BOOL WriteExact(HANDLE h, const void* buffer, DWORD bytes)
{
    DWORD got = 0;
    return WriteFile(h, buffer, bytes, &got, 0) && got == bytes;
}

static BOOL SeekAbs(HANDLE h, DWORD offset)
{
    DWORD pos = SetFilePointer(h, (LONG)offset, 0, FILE_BEGIN);
    return pos != INVALID_SET_FILE_POINTER || GetLastError() == NO_ERROR;
}

static BOOL GetFileSize32(HANDLE h, DWORD* outSize)
{
    DWORD high = 0;
    DWORD low = GetFileSize(h, &high);

    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
        return FALSE;

    if (high != 0)
        return FALSE;

    *outSize = low;
    return TRUE;
}

typedef struct _PE_INFO {
    WORD Machine;
    WORD Characteristics;
    WORD Magic;
    WORD MajorOS;
    WORD MinorOS;
    WORD MajorSubsystem;
    WORD MinorSubsystem;
    WORD Subsystem;
    DWORD OptionalOffset;
    WORD OptionalSize;
    DWORD ChecksumOffset;
    DWORD SecurityVA;
    DWORD SecuritySize;
} PE_INFO;

static BOOL ReadPeInfo(HANDLE h, DWORD fileSize, PE_INFO* pi)
{
    IMAGE_DOS_HEADER dos;
    DWORD signature;
    IMAGE_FILE_HEADER fh;
    WORD magic;
    BYTE optional[256];
    DWORD optionalOffset;
    DWORD securityOffset;

    if (!pi || fileSize < sizeof(IMAGE_DOS_HEADER))
        return FALSE;

    if (!SeekAbs(h, 0) || !ReadExact(h, &dos, sizeof(dos)))
        return FALSE;

    if (dos.e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;

    if (dos.e_lfanew < 0)
        return FALSE;

    if ((DWORD)dos.e_lfanew > fileSize - 4 - sizeof(IMAGE_FILE_HEADER))
        return FALSE;

    if (!SeekAbs(h, (DWORD)dos.e_lfanew))
        return FALSE;

    if (!ReadExact(h, &signature, sizeof(signature)))
        return FALSE;

    if (signature != IMAGE_NT_SIGNATURE)
        return FALSE;

    if (!ReadExact(h, &fh, sizeof(fh)))
        return FALSE;

    optionalOffset = (DWORD)dos.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER);

    if (fh.SizeOfOptionalHeader < 0x44 ||
        optionalOffset > fileSize ||
        fh.SizeOfOptionalHeader > fileSize - optionalOffset ||
        fh.SizeOfOptionalHeader > sizeof(optional))
        return FALSE;

    if (!ReadExact(h, optional, fh.SizeOfOptionalHeader))
        return FALSE;

    magic = *(WORD*)(optional + 0);

    if (magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return FALSE;

    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        if (fh.SizeOfOptionalHeader < 0x88)
            return FALSE;
        securityOffset = 0x60 + (IMAGE_DIRECTORY_ENTRY_SECURITY * 8);
    } else {
        if (fh.SizeOfOptionalHeader < 0x98)
            return FALSE;
        securityOffset = 0x70 + (IMAGE_DIRECTORY_ENTRY_SECURITY * 8);
    }

    pi->Machine = fh.Machine;
    pi->Characteristics = fh.Characteristics;
    pi->Magic = magic;
    pi->MajorOS = *(WORD*)(optional + 0x28);
    pi->MinorOS = *(WORD*)(optional + 0x2A);
    pi->MajorSubsystem = *(WORD*)(optional + 0x30);
    pi->MinorSubsystem = *(WORD*)(optional + 0x32);
    pi->Subsystem = *(WORD*)(optional + 0x44);
    pi->OptionalOffset = optionalOffset;
    pi->OptionalSize = fh.SizeOfOptionalHeader;
    pi->ChecksumOffset = optionalOffset + 0x40;
    pi->SecurityVA = *(DWORD*)(optional + securityOffset);
    pi->SecuritySize = *(DWORD*)(optional + securityOffset + 4);

    return TRUE;
}

static const WCHAR* MachineName(WORD machine)
{
    if (machine == IMAGE_FILE_MACHINE_I386)
        return L"x86";
    if (machine == IMAGE_FILE_MACHINE_AMD64)
        return L"x64";
    return L"unsupported";
}

static const WCHAR* PeKindName(WORD magic)
{
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return L"PE32";
    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return L"PE32+";
    return L"?";
}

static const WCHAR* SubsystemName(WORD subsystem)
{
    switch (subsystem) {
    case IMAGE_SUBSYSTEM_NATIVE: return L"Native";
    case IMAGE_SUBSYSTEM_WINDOWS_GUI: return L"Windows GUI";
    case IMAGE_SUBSYSTEM_WINDOWS_CUI: return L"Windows CUI";
    default: return L"Other";
    }
}

static BOOL BuildBackupName(const WCHAR* file, WCHAR* out, SIZE_T cap)
{
    SIZE_T n = WLen(file);

    if (n + 5 >= cap)
        return FALSE;

    WCopy(out, cap, file);
    WCat(out, cap, L".bak");
    return TRUE;
}

static DWORD ComputePeChecksum(HANDLE h, DWORD fileSize, DWORD checksumOffset)
{
    DWORD offset = 0;
    DWORD sum = 0;

    if (!SeekAbs(h, 0))
        return 0;

    while (offset < fileSize) {
        DWORD want = fileSize - offset;
        DWORD got = 0;
        DWORD i;

        if (want > sizeof(gChecksumBuffer))
            want = sizeof(gChecksumBuffer);

        if (!ReadFile(h, gChecksumBuffer, want, &got, 0) || got != want)
            return 0;

        i = 0;
        while (i < got) {
            DWORD absolute = offset + i;
            WORD word = 0;

            if (absolute == checksumOffset) {
                i += 4;
                continue;
            }

            word = gChecksumBuffer[i];
            if (i + 1 < got)
                word |= (WORD)((WORD)gChecksumBuffer[i + 1] << 8);

            sum += word;
            sum = (sum & 0xFFFFUL) + (sum >> 16);
            i += 2;
        }

        offset += got;
    }

    sum = (sum & 0xFFFFUL) + (sum >> 16);
    sum = sum + (sum >> 16);
    sum = sum & 0xFFFFUL;
    sum += fileSize;

    return sum;
}

static BOOL PatchVersions(HANDLE h, PE_INFO* pi,
                          WORD major, WORD minor,
                          DWORD fileSize)
{
    DWORD checksum;
    DWORD zero = 0;

    if (!SeekAbs(h, pi->OptionalOffset + 0x28))
        return FALSE;
    if (!WriteExact(h, &major, sizeof(major)) ||
        !WriteExact(h, &minor, sizeof(minor)))
        return FALSE;

    if (!SeekAbs(h, pi->OptionalOffset + 0x30))
        return FALSE;
    if (!WriteExact(h, &major, sizeof(major)) ||
        !WriteExact(h, &minor, sizeof(minor)))
        return FALSE;

    if (!SeekAbs(h, pi->ChecksumOffset))
        return FALSE;
    if (!WriteExact(h, &zero, sizeof(zero)))
        return FALSE;

    FlushFileBuffers(h);

    checksum = ComputePeChecksum(h, fileSize, pi->ChecksumOffset);

    if (!SeekAbs(h, pi->ChecksumOffset))
        return FALSE;
    if (!WriteExact(h, &checksum, sizeof(checksum)))
        return FALSE;

    FlushFileBuffers(h);
    return TRUE;
}

static void PrintInfo(const PE_INFO* pi)
{
    Out(L"Format:       ");
    Out(PeKindName(pi->Magic));
    Out(L" (");
    Out(MachineName(pi->Machine));
    OutLn(L")");

    Out(L"Subsystem:    ");
    OutLn(SubsystemName(pi->Subsystem));

    Out(L"OS version:   ");
    PrintVersionPair(pi->MajorOS, pi->MinorOS);
    OutLn(L"");

    Out(L"Subsystem ver:");
    Out(L" ");
    PrintVersionPair(pi->MajorSubsystem, pi->MinorSubsystem);
    OutLn(L"");

    Out(L"Signed:       ");
    OutLn((pi->SecurityVA && pi->SecuritySize) ? L"yes" : L"no");
}

static int PPMain(int argc, WCHAR** argv)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD fileSize = 0;
    PE_INFO pi;
    BOOL inspectOnly = FALSE;
    BOOL customVersion = FALSE;
    WORD newMajor = 0, newMinor = 0;
    WCHAR backup[MAX_PATH * 4];

    if (argc != 2 && argc != 3) {
        PrintUsage();
        return 2;
    }

    if (argc == 3) {
        if ((argv[2][0] == L's' || argv[2][0] == L'S') && argv[2][1] == 0) {
            inspectOnly = TRUE;
        } else {
            if (!ParseVersion(argv[2], &newMajor, &newMinor)) {
                ErrLn(L"Invalid NT version. Use major.minor, for example 3.50 or 5.02.");
                return 2;
            }
            customVersion = TRUE;
        }
    }

    h = CreateFileW(argv[1],
                    inspectOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE),
                    FILE_SHARE_READ,
                    0,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    0);

    if (h == INVALID_HANDLE_VALUE) {
        ErrLn(L"Unable to open file.");
        return 3;
    }

    if (!GetFileSize32(h, &fileSize) ||
        !ReadPeInfo(h, fileSize, &pi)) {
        CloseHandle(h);
        ErrLn(L"PE header is not sufficiently readable to locate version fields.");
        return 4;
    }

    PrintInfo(&pi);

    if (inspectOnly) {
        CloseHandle(h);
        return 0;
    }

    if (!customVersion) {
        if (pi.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            newMajor = 3;
            newMinor = 50;
        } else if (pi.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            newMajor = 5;
            newMinor = 2;
        } else {
            CloseHandle(h);
            ErrLn(L"Unsupported Optional Header format.");
            return 5;
        }
    }

    if (pi.SecurityVA && pi.SecuritySize) {
        OutLn(L"Warning: this image appears Authenticode-signed.");
        OutLn(L"         Patching will invalidate the existing signature.");
    }

    if (pi.Characteristics & IMAGE_FILE_DLL)
        OutLn(L"Warning: target image is marked as a DLL.");

    if (!BuildBackupName(argv[1], backup,
                         sizeof(backup) / sizeof(backup[0]))) {
        CloseHandle(h);
        ErrLn(L"Path is too long to create .bak backup.");
        return 10;
    }

    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;

    if (!CopyFileW(argv[1], backup, TRUE)) {
        ErrLn(L"Unable to create backup. If .bak already exists, delete/rename it first.");
        return 11;
    }

    h = CreateFileW(argv[1],
                    GENERIC_READ | GENERIC_WRITE,
                    0,
                    0,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    0);

    if (h == INVALID_HANDLE_VALUE) {
        ErrLn(L"Unable to reopen file for patching.");
        return 12;
    }

    if (!GetFileSize32(h, &fileSize) ||
        !ReadPeInfo(h, fileSize, &pi)) {
        CloseHandle(h);
        ErrLn(L"PE header became unreadable after backup.");
        return 13;
    }

    if (!PatchVersions(h, &pi, newMajor, newMinor, fileSize)) {
        CloseHandle(h);
        ErrLn(L"Patch failed. Original backup remains available.");
        return 14;
    }

    CloseHandle(h);

    Out(L"Patched to:   ");
    PrintVersionPair(newMajor, newMinor);
    OutLn(L"");
    Out(L"Backup:       ");
    OutLn(backup);

    return 0;
}

extern "C" void WINAPI PPEntry()
{
    WCHAR** argv = 0;
    WCHAR* storage = 0;
    int argc;
    int rc;

    gHeap = GetProcessHeap();
    if (!gHeap)
        ExitProcess(100);

    argc = ParseCommandLine(&argv, &storage);
    if (!argc) {
        PrintUsage();
        ExitProcess(101);
    }

    rc = PPMain(argc, argv);

    if (argv) HeapFree(gHeap, 0, argv);
    if (storage) HeapFree(gHeap, 0, storage);

    ExitProcess((UINT)rc);
}