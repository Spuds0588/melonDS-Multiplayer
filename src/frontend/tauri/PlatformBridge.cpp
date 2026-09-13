/*
    melonDS Multiplayer - Platform backend for the Tauri host.

    melonDS's core is frontend agnostic: it declares a set of functions in
    Platform.h that the embedding application has to provide (file I/O, threads,
    logging, save persistence, multiplayer comm, ...). This file implements them
    on top of the C++ standard library so the core can run inside the Tauri
    process without pulling in Qt or SDL.
*/

#include "Platform.h"
#include "melonds_bridge.h"
#include "BridgeInternal.h"

#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <vector>

#include "LocalMP.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace melonDS
{
namespace Platform
{

/* -------------------------------------------------------------------- logging */

void Log(LogLevel level, const char* fmt, ...)
{
    static const char* prefixes[] = { "Debug", "Info", "Warn", "Error" };

    const char* prefix = "Log";
    if ((int)level >= 0 && (int)level < 4) prefix = prefixes[(int)level];

    std::fprintf(stderr, "[melonDS %s] ", prefix);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}

/* ---------------------------------------------------------------- file access */

struct FileHandle
{
    FILE* fp;
};

namespace
{

std::string config_dir()
{
    if (const char* override_dir = std::getenv("MELONDS_MULTIPLAYER_DIR"))
        return std::string(override_dir);

    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"))
        home = appdata;
#endif
    if (!home || !*home) home = ".";

    return std::string(home) + "/.melonDS-multiplayer";
}

const char* mode_to_c(FileMode mode)
{
    const bool read = (mode & Read) != 0;
    const bool write = (mode & Write) != 0;
    const bool preserve = (mode & Preserve) != 0;
    const bool nocreate = (mode & NoCreate) != 0;
    const bool append = (mode & Append) != 0;

    if (read && write) return (preserve || nocreate) ? "r+b" : "w+b";
    if (append) return "ab";
    if (write) return preserve ? "r+b" : "wb";
    return "rb";
}

} // namespace

std::string GetLocalFilePath(const std::string& filename)
{
    return config_dir() + "/" + filename;
}

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    const bool preserve = (mode & Preserve) != 0;
    const bool nocreate = (mode & NoCreate) != 0;
    const bool write = (mode & Write) != 0;

    FILE* fp = std::fopen(path.c_str(), mode_to_c(mode));

    /* Write|Preserve means "open existing, create if missing, don't truncate". */
    if (!fp && write && preserve && !nocreate)
        fp = std::fopen(path.c_str(), "w+b");

    if (!fp) return nullptr;

    auto* handle = new FileHandle { fp };
    return handle;
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode)
{
    std::error_code ec;
    std::filesystem::create_directories(config_dir(), ec);
    return OpenFile(GetLocalFilePath(path), mode);
}

bool FileExists(const std::string& name)
{
    FILE* fp = std::fopen(name.c_str(), "rb");
    if (!fp) return false;
    std::fclose(fp);
    return true;
}

bool LocalFileExists(const std::string& name)
{
    return FileExists(GetLocalFilePath(name));
}

bool CheckFileWritable(const std::string& filepath)
{
    FILE* fp = std::fopen(filepath.c_str(), "ab");
    if (!fp) return false;
    std::fclose(fp);
    return true;
}

bool CheckLocalFileWritable(const std::string& filepath)
{
    std::error_code ec;
    std::filesystem::create_directories(config_dir(), ec);
    return CheckFileWritable(GetLocalFilePath(filepath));
}

bool CloseFile(FileHandle* file)
{
    if (!file) return false;
    const bool ok = std::fclose(file->fp) == 0;
    delete file;
    return ok;
}

bool IsEndOfFile(FileHandle* file)
{
    if (!file) return true;
    return std::feof(file->fp) != 0;
}

bool FileReadLine(char* str, int count, FileHandle* file)
{
    if (!file || !str || count <= 0) return false;
    return std::fgets(str, count, file->fp) != nullptr;
}

u64 FilePosition(FileHandle* file)
{
    if (!file) return 0;
    const long pos = std::ftell(file->fp);
    return pos < 0 ? 0 : (u64)pos;
}

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin)
{
    if (!file) return false;

    int whence = SEEK_SET;
    switch (origin)
    {
    case FileSeekOrigin::Current: whence = SEEK_CUR; break;
    case FileSeekOrigin::End:     whence = SEEK_END; break;
    case FileSeekOrigin::Start:   whence = SEEK_SET; break;
    }

    return std::fseek(file->fp, (long)offset, whence) == 0;
}

void FileRewind(FileHandle* file)
{
    if (!file) return;
    std::rewind(file->fp);
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* file)
{
    if (!file) return 0;
    return (u64)std::fread(data, (size_t)size, (size_t)count, file->fp);
}

bool FileFlush(FileHandle* file)
{
    if (!file) return false;
    return std::fflush(file->fp) == 0;
}

u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file)
{
    if (!file) return 0;
    return (u64)std::fwrite(data, (size_t)size, (size_t)count, file->fp);
}

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...)
{
    if (!file || !fmt) return 0;

    va_list args;
    va_start(args, fmt);
    const int wrote = std::vfprintf(file->fp, fmt, args);
    va_end(args);

    return wrote < 0 ? 0 : (u64)wrote;
}

u64 FileLength(FileHandle* file)
{
    if (!file) return 0;

    const long pos = std::ftell(file->fp);
    if (pos < 0) return 0;

    if (std::fseek(file->fp, 0, SEEK_END) != 0) return 0;
    const long len = std::ftell(file->fp);
    std::fseek(file->fp, pos, SEEK_SET);

    return len < 0 ? 0 : (u64)len;
}

/* --------------------------------------------------------------------- threads */

struct Thread
{
    std::thread handle;
};

Thread* Thread_Create(std::function<void()> func)
{
    auto* thread = new Thread { std::thread(std::move(func)) };
    return thread;
}

void Thread_Free(Thread* thread)
{
    if (!thread) return;
    if (thread->handle.joinable())
        thread->handle.join();
    delete thread;
}

void Thread_Wait(Thread* thread)
{
    if (!thread) return;
    if (thread->handle.joinable())
        thread->handle.join();
}

struct Semaphore
{
    std::mutex lock;
    std::condition_variable cv;
    int count = 0;
};

Semaphore* Semaphore_Create()
{
    return new Semaphore();
}

void Semaphore_Free(Semaphore* sema)
{
    delete sema;
}

void Semaphore_Reset(Semaphore* sema)
{
    if (!sema) return;
    std::lock_guard<std::mutex> lock(sema->lock);
    sema->count = 0;
}

void Semaphore_Wait(Semaphore* sema)
{
    if (!sema) return;
    std::unique_lock<std::mutex> lock(sema->lock);
    sema->cv.wait(lock, [sema] { return sema->count > 0; });
    sema->count--;
}

bool Semaphore_TryWait(Semaphore* sema, int timeout_ms)
{
    if (!sema) return false;

    std::unique_lock<std::mutex> lock(sema->lock);

    if (sema->count > 0)
    {
        sema->count--;
        return true;
    }

    if (timeout_ms == 0)
        return false;

    const bool signaled = sema->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                            [sema] { return sema->count > 0; });
    if (!signaled) return false;

    sema->count--;
    return true;
}

void Semaphore_Post(Semaphore* sema, int count)
{
    if (!sema) return;
    std::lock_guard<std::mutex> lock(sema->lock);
    sema->count += count;
    if (count > 0)
        sema->cv.notify_all();
}

struct Mutex
{
    std::mutex handle;
};

Mutex* Mutex_Create()
{
    return new Mutex();
}

void Mutex_Free(Mutex* mutex)
{
    delete mutex;
}

void Mutex_Lock(Mutex* mutex)
{
    if (mutex) mutex->handle.lock();
}

void Mutex_Unlock(Mutex* mutex)
{
    if (mutex) mutex->handle.unlock();
}

bool Mutex_TryLock(Mutex* mutex)
{
    if (!mutex) return false;
    return mutex->handle.try_lock();
}

void Sleep(u64 usecs)
{
    std::this_thread::sleep_for(std::chrono::microseconds(usecs));
}

u64 GetMSCount()
{
    using namespace std::chrono;
    return (u64)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

u64 GetUSCount()
{
    using namespace std::chrono;
    return (u64)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

/* ------------------------------------------------------------- save persistence */

void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    md::notify_save_write(userdata, savedata, savelen, writeoffset, writelen);
}

void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    md::notify_gba_save_write(userdata, savedata, savelen, writeoffset, writelen);
}

void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata)
{
    md::notify_firmware_write(userdata, firmware, writeoffset, writelen);
}

void WriteDateTime(int year, int month, int day, int hour, int minute, int second, void* userdata)
{
    (void)year; (void)month; (void)day;
    (void)hour; (void)minute; (void)second;
    (void)userdata;
    /* The emulated RTC simply follows the host clock in v1. */
}

/* ------------------------------------------------------------ stop signalling */

void SignalStop(StopReason reason, void* userdata)
{
    md::notify_stop(userdata, (int)reason + MD_STOP_EXTERNAL);
}

/* --------------------------------------------------------- local multiplayer */

void MP_Begin(void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (bus && slot >= 0) bus->Begin(slot);
}

void MP_End(void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (bus && slot >= 0) bus->End(slot);
}

int MP_SendPacket(u8* data, int len, u64 timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;
    return bus->SendPacket(slot, data, len, timestamp);
}

int MP_RecvPacket(u8* data, u64* timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;
    return bus->RecvPacket(slot, data, timestamp);
}

int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;
    return bus->SendCmd(slot, data, len, timestamp);
}

int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;
    return bus->SendReply(slot, data, len, timestamp, aid);
}

int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;
    return bus->SendAck(slot, data, len, timestamp);
}

int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;
    return bus->RecvHostPacket(slot, data, timestamp);
}

u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;
    return bus->RecvReplies(slot, data, timestamp, aidmask);
}

/* ---------------------------------------------------------------- internet comm */

int Net_SendPacket(u8* data, int len, void* userdata)
{
    (void)data; (void)len; (void)userdata;
    /* Online play is a later phase; the DS is effectively offline. */
    return 0;
}

int Net_RecvPacket(u8* data, void* userdata)
{
    (void)data; (void)userdata;
    return 0;
}

/* ------------------------------------------------------------------- cameras */

void Camera_Start(int num, void* userdata)
{
    (void)num; (void)userdata;
}

void Camera_Stop(int num, void* userdata)
{
    (void)num; (void)userdata;
}

void Camera_CaptureFrame(int num, u32* frame, int width, int height, bool yuv, void* userdata)
{
    (void)num; (void)userdata; (void)yuv;
    if (!frame) return;
    std::memset(frame, 0, (size_t)width * (size_t)height * sizeof(u32));
}

/* --------------------------------------------------------------- microphone */

void Mic_Start(void* userdata)
{
    (void)userdata;
}

void Mic_Stop(void* userdata)
{
    (void)userdata;
}

int Mic_ReadInput(s16* data, int maxlength, void* userdata)
{
    (void)userdata;
    if (data && maxlength > 0)
        std::memset(data, 0, (size_t)maxlength * sizeof(s16));
    return 0;
}

/* ----------------------------------------------------------------- AAC (DSi) */

AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder* dec) { (void)dec; }
bool AAC_Configure(AACDecoder* dec, int frequency, int channels)
{
    (void)dec; (void)frequency; (void)channels;
    return false;
}
bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen, void* output, int outputlen)
{
    (void)dec; (void)input; (void)inputlen; (void)output; (void)outputlen;
    return false;
}

/* ------------------------------------------------------------------- addons */

bool Addon_KeyDown(KeyType type, void* userdata)
{
    (void)type; (void)userdata;
    return false;
}

void Addon_RumbleStart(u32 len, void* userdata)
{
    (void)len; (void)userdata;
}

void Addon_RumbleStop(void* userdata)
{
    (void)userdata;
}

float Addon_MotionQuery(MotionQueryType type, void* userdata)
{
    (void)type; (void)userdata;
    return 0.0f;
}

/* ---------------------------------------------------------- dynamic libraries */

struct DynamicLibrary
{
#ifdef _WIN32
    HMODULE handle;
#else
    void* handle;
#endif
};

DynamicLibrary* DynamicLibrary_Load(const char* lib)
{
    if (!lib) return nullptr;
#ifdef _WIN32
    HMODULE handle = LoadLibraryA(lib);
    if (!handle) return nullptr;
    return new DynamicLibrary { handle };
#else
    void* handle = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return nullptr;
    return new DynamicLibrary { handle };
#endif
}

void DynamicLibrary_Unload(DynamicLibrary* lib)
{
    if (!lib) return;
#ifdef _WIN32
    FreeLibrary(lib->handle);
#else
    dlclose(lib->handle);
#endif
    delete lib;
}

void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name)
{
    if (!lib || !name) return nullptr;
#ifdef _WIN32
    return (void*)GetProcAddress(lib->handle, name);
#else
    return dlsym(lib->handle, name);
#endif
}

} // namespace Platform
} // namespace melonDS
