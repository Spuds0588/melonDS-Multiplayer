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

/* Tag for core log lines emitted by the current thread. Each console runs on
   its own thread ("playerN" from the Rust host, "consoleN" from the C++
   harness), so this disambiguates interleaved wifi logs: which console was
   on the wrong channel, which one powered its radio on, etc. */
static thread_local std::string t_log_tag = "main";

/* namespace-visible setter used by the extern "C" md_tag_current_thread. */
void set_thread_log_tag(const char* tag)
{
    t_log_tag = tag ? tag : "?";
}

void Log(LogLevel level, const char* fmt, ...)
{
    static const char* prefixes[] = { "Debug", "Info", "Warn", "Error" };

    const char* prefix = "Log";
    if ((int)level >= 0 && (int)level < 4) prefix = prefixes[(int)level];

    std::fprintf(stderr, "[melonDS %s:%s] ", t_log_tag.c_str(), prefix);

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

/* ------------------------------------------------------------ frame tracing */

/* MD_LINK_VERBOSE=1 makes the virtual air narrate every frame it carries, from
   both ends: what a console put on the wire and what each peer pulled off it.
   The counters in MDLinkStats only say *that* traffic stalled; these lines say
   why - a joiner that answers nothing, a room beacon whose host MAC is also the
   receiver's own, a reply that never reaches the host. Two consoles' worth of
   discovery is a few dozen frames, so the noise is affordable exactly when a
   lobby refuses to fill. */
static bool link_trace_enabled()
{
    static const bool enabled = []() {
        const char* v = std::getenv("MD_LINK_VERBOSE");
        return v && *v && (*v != '0');
    }();
    return enabled;
}

static void format_mac(const u8* p, char* out) /* out must hold 13 bytes */
{
    std::snprintf(out, 13, "%02X%02X%02X%02X%02X%02X", p[0], p[1], p[2], p[3], p[4], p[5]);
}

/* data is the DS-style 12 byte TX header + the 802.11 frame, i.e. exactly what
   the wireless hardware hands the platform layer. The header fields printed are
   the ones discovery and the MP handshake turn on: the frame control word, the
   three address fields (received-from, transmitted-by, BSSID), the sequence
   control word and, for MP frames, the sync / client-mask block at the start of
   the payload.

   `seq` earns its place because the DS firmware throws away a CMD frame whose
   sequence number repeats the previous one (see Wifi::FinishRX's cmd_dupe), so
   a host stuck on one sequence number looks like a host that never speaks.

   `t` is the emulated US-timer timestamp the frame carries. The two consoles
   are only locked to the same *frame*, so their US clocks sit at whatever phase
   within the frame they each reached; printing both ends' timestamps is what
   turns "the reply came too late" into a number instead of a guess. */
static void trace_frame(const char* dir, int slot, const char* kind, u64 timestamp,
                        const u8* data, int len)
{
    if (!link_trace_enabled()) return;

    if (!data || len < 12)
    {
        /* The blank MP reply ("nothing to send", see Wifi::FinishRX) is a zero
           length frame, invisible to a trace that only prints whole frames.
           The host reads it as a packet but, having no payload, not as a reply -
           so it reports the client as failed while the wire shows no gap. */
        std::fprintf(stderr, "[link-trace] p%d %s %-5s len=%3d t=%llu (empty frame)\n",
                     slot + 1, dir, kind, len, (unsigned long long)timestamp);
        return;
    }

    const u8* frame = data + 12;
    const int framelen = len - 12;

    const u16 framectl = (framelen >= 2) ? *(const u16*)&frame[0] : 0;
    const u16 seq = (framelen >= 24) ? *(const u16*)&frame[22] : 0;

    /* The body of every *new* command a game sends, one line per sequence
       number: retries repeat the same seqno, so this stays small while showing
       exactly what one game asks of the other. This is the missing half of the
       2026-09-25 MKDS investigation - the transport is at reference quality
       (v0.1.8q), the guest's game answers every poll from one frozen template
       and then leaves after ~4.3s, and whether the commands' *content* is what
       its game expects is the open question. MP commands only (`fc=0228`); a
       new seqno during multiplayer is one line, everything else stays off the
       log. */
    if (framelen >= 30 && (framectl & 0x00FF) == 0x28 && (framectl & 0x0300) == 0x0200)
    {
        static u16 lastcmdseq = 0xFFFF;
        static int lastcmdslot = -1;
        if (seq != lastcmdseq || slot != lastcmdslot)
        {
            lastcmdseq = seq;
            lastcmdslot = slot;
            const int bodylen = framelen - 28;
            char body[3 * 18 + 1] = "-";
            if (bodylen > 0)
            {
                const int n = (bodylen > 18) ? 18 : bodylen;
                for (int i = 0; i < n; i++)
                    snprintf(&body[i * 3], 4, "%02X ", frame[28 + i]);
                body[n * 3 - 1] = 0;
            }
            std::fprintf(stderr, "[link-trace] cmd body seq=%04X slot=%d body[%d]=%s\n",
                         seq, slot + 1, bodylen, body);
        }
    }
    char rxmac[13] = "------------";
    char txmac[13] = "------------";
    char bssid[13] = "------------";
    if (framelen >= 10) format_mac(&frame[4], rxmac);
    if (framelen >= 16) format_mac(&frame[10], txmac);
    if (framelen >= 22) format_mac(&frame[16], bssid);

    std::fprintf(stderr,
                 "[link-trace] p%d %s %-5s len=%3d chan=%2d fc=%04X seq=%04X rx=%s tx=%s bssid=%s t=%llu",
                 slot + 1, dir, kind, len, data[9], framectl, seq, rxmac, txmac, bssid,
                 (unsigned long long)timestamp);

    if (framelen >= 32)
    {
        const u64 sync = *(const u64*)&frame[24];
        const u16 mask = *(const u16*)&frame[26];
        std::fprintf(stderr, " sync=%016llX mask=%04X",
                     (unsigned long long)sync, (unsigned)mask);
    }

    std::fprintf(stderr, "\n");
}

/* --------------------------------------------------------- local multiplayer */

/* The console powers its wireless hardware on and off as the game enters and
   leaves wireless play; this is where it joins and leaves the shared bus. The
   bridge notes the transition so a host can tell which slots are actually live
   rather than merely assigned. */
void MP_Begin(void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return;

    bus->Begin(slot);
    md::notify_mp_begin(userdata);
}

void MP_End(void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return;

    bus->End(slot);
    md::notify_mp_end(userdata);
}

int MP_SendPacket(u8* data, int len, u64 timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;

    const int queued = bus->SendPacket(slot, data, len, timestamp);
    md::note_packet_sent_kind(userdata, 0, queued);
    trace_frame("tx", slot, "bcast", timestamp, data, len);
    return queued;
}

int MP_RecvPacket(u8* data, u64* timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;

    const int got = bus->RecvPacket(slot, data, timestamp);
    md::note_poll(userdata, got > 0);
    if (got > 0)
    {
        md::note_packet_received_kind(userdata, 0, got);
        trace_frame("rx", slot, "bcast", timestamp ? *timestamp : 0, data, got);
    }
    return got;
}

int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;

    const int queued = bus->SendCmd(slot, data, len, timestamp);
    md::note_packet_sent_kind(userdata, 1, queued);
    trace_frame("tx", slot, "cmd", timestamp, data, len);
    return queued;
}

int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;

    const int queued = bus->SendReply(slot, data, len, timestamp, aid);
    md::note_packet_sent_kind(userdata, 2, queued);
    trace_frame("tx", slot, "reply", timestamp, data, len);
    return queued;
}

int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;

    const int queued = bus->SendAck(slot, data, len, timestamp);
    md::note_packet_sent_kind(userdata, 3, queued);
    trace_frame("tx", slot, "ack", timestamp, data, len);
    return queued;
}

int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;

    const int got = bus->RecvHostPacket(slot, data, timestamp);
    md::note_poll(userdata, got > 0);
    if (got > 0)
    {
        md::note_packet_received_kind(userdata, 1, got);
        trace_frame("rx", slot, "cmd", timestamp ? *timestamp : 0, data, got);
    }
    return got;
}

u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata)
{
    auto* bus = md::link_bus();
    const int slot = md::slot_for(userdata);
    if (!bus || slot < 0) return 0;

    const u16 replied = bus->RecvReplies(slot, data, timestamp, aidmask);

    /* Counted as a packet but not as bytes: this one reports which clients
       replied rather than how much was read, so a byte total would be a guess.
       Classed as reply traffic (type 2) in the per-kind split. */
    md::note_poll(userdata, replied > 0);
    if (replied)
    {
        md::note_packet_received_kind(userdata, 2, 0);

        /* Only the mask is printed: this call reports which clients replied
           rather than how much was read, so the reply bodies live in the
           caller's per-AID blocks and are not ours to size. */
        if (link_trace_enabled())
            std::fprintf(stderr, "[link-trace] p%d rx replies mask=%04X\n",
                         slot + 1, (unsigned)replied);
    }
    return replied;
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

/* ------------------------------------------------------------------ C API */

/* Tag for core log lines emitted by the current thread (see t_log_tag above).
   extern "C" so the Rust host links it by name like the rest of md_*. */
void md_tag_current_thread(const char* tag)
{
    melonDS::Platform::set_thread_log_tag(tag);
}
