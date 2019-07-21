/**
 * We want to read arbitrary integers from a file. This is a very small example
 * of what handling a binary file in a long running process might look like,
 * meant to simulate what it's like reading git object files in Sublime Merge.
 *
 * This is only tested for linux, however it may compile/run on mac/windows.
 */
#include <iostream>
#include <random>

#include <assert.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
void install_signal_handlers() {}
#else
// Keep track of this thread's jump point and whether it's set
thread_local volatile bool sigbus_jmp_set;
thread_local sigjmp_buf sigbus_jmp_buf;

static void handle_sigbus(int c) {
    // Only handle the signal if the jump point is set on this thread
    if (sigbus_jmp_set) {
        sigbus_jmp_set = false;

        // siglongjmp out of the signal handler, returning the signal
        siglongjmp(sigbus_jmp_buf, c);
    }
}

void install_signal_handlers() {
    // Install signal handler for SIGBUS
    struct sigaction act;
    act.sa_handler = &handle_sigbus;

    // SA_NODEFER is required due to siglongjmp
    act.sa_flags = SA_NODEFER;
    sigemptyset(&act.sa_mask); // Don't block any signals

    // Connect the signal
    sigaction(SIGBUS, &act, nullptr);
}
#endif


template<typename F>
bool safe_mmap_try(F fn) {
#if defined(_WIN32)
    __try {
        fn();
        return true;
    } __except(
        GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
            ? EXCEPTION_EXECUTE_HANDLER
            : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
#else
    // Make sure we don't call safe_mmap_try from fn
    assert(!sigbus_jmp_set);

    sigbus_jmp_set = true;

    // sigsetjmp to handle SIGBUS. Do not save the signal mask
    if (sigsetjmp(sigbus_jmp_buf, 0) == 0) {
        // Call the lambda
        fn();

        // Notify that a jmp point has been set.
        sigbus_jmp_set = false;
        return true;
    } else {
        sigbus_jmp_set = false;
        return false;
    }
#endif
}

struct file {
    const size_t size;
    const void* data;

    // File constructor
    file(size_t s, void* d) : size(s), data(d) {
    }

    // Virtual file destructor so we can override per system
    virtual ~file() {}

    // Get a 64 bit integer at the byte offset
    bool read(size_t offset, int64_t * result) {
        // Out of bounds check
        assert(offset <= size - sizeof(int64_t));

        return safe_mmap_try([&]() {
            *result = *(int64_t*)((int8_t*)data + offset);
        });
    }
};

#if defined(_WIN32)
struct windows_file : public file {
    HANDLE win_handle;

    windows_file(HANDLE h, size_t s, void* d) : file(s, d), win_handle(h) {
    }

    virtual ~windows_file() {
        // Need to unmap, then close
        UnmapViewOfFile(data);
        CloseHandle(win_handle);
    }
};

file* open_file(const char * path) {
    // Create a normal file handle
    HANDLE f = CreateFile(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return nullptr;

    // Get the size of the file
    size_t size = 0;

    LARGE_INTEGER i;
    if (GetFileSizeEx(f, &i)) {
        size = (size_t)i.QuadPart;
    } else {
        CloseHandle(f);
        return nullptr;
    }

    // Create a file mapping, needed for a map view
    HANDLE hmap = CreateFileMapping(f, nullptr, PAGE_READONLY, 0, 0, nullptr);

    if (!hmap)
        return nullptr;

    // Actually memory map the file
    void* data = MapViewOfFile(hmap, FILE_MAP_READ, 0, 0, size);

    // Close the regular file handle, keep hmap around
    CloseHandle(f);

    if (!data) {
        CloseHandle(hmap);
        return nullptr;
    }

    return new windows_file(hmap, size, data);
}
#else
struct posix_file : public file {
    using file::file;

    virtual ~posix_file() {
        munmap((void*)data, size);
    }
};

file* open_file(const char * path) {
    // Stat the file to get the size for later
    struct stat64 st;

    if (stat64(path, &st))
        return nullptr;

    // Open the file in read only mode
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return nullptr;

    // Allocate a buffer for the file contents
    void* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    // mmap returns MAP_FAILED on error, not NULL
    if (data == MAP_FAILED)
        return nullptr;

    // Construct a new file with the data
    return new posix_file(st.st_size, data);
}
#endif

int main(int argc, char const *argv[]) {
    // Assume we're given 1 argument
    if (argc != 2) {
        return 1;
    }

    install_signal_handlers();

    // Open the requested file
    file* f = open_file(argv[1]);

    // Setup some random number generation
    std::mt19937 rng;
    rng.seed(std::random_device()());
    auto random = std::uniform_int_distribution<std::mt19937::result_type>(
        0, f->size - sizeof(int64_t));

    // Continuously read from a random location
    while (true) {
        size_t offset = (size_t) random(rng);

        // Get the number at the offset
        int64_t value;
        if (f->read(offset, &value)) {
            // Print out the number
            std::cout << value << std::endl;
        } else {
            std::cout << "Failed to read" << std::endl;
        }
    }

    delete f;

    return 0;
}
