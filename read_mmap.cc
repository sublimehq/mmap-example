/**
 * We want to read arbitrary integers from a file. This is a very small example
 * of what handling a binary file in a long running process might look like,
 * meant to simulate what its like reading git object files in Sublime Merge.
 *
 * This is only tested for linux, however it may compile/run on mac/windows.
 */
#include <iostream>
#include <random>

#include <assert.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Keep track of this thread's jump point and whether its set
thread_local volatile bool sigbus_jmp_set;
thread_local sigjmp_buf sigbus_jmp_buf;

static void handle_sigbus(int c) {
    // Onlt handle the signal if the jump point is set on this thread
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

template<typename F>
bool safe_mmap_try(F fn) {
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
}

struct file {
    const size_t size;
    const void* data;

    // File constructor
    file(size_t s, void* d) : size(s), data(d) {
    }

    // File destructor
    ~file() {
        munmap((void*)data, size);
    }

    // Get a 64 bit integer at the byte offset
    bool read(size_t offset, int64_t * result) {
        // Out of bounds check
        assert(offset <= size - sizeof(int64_t));

        return safe_mmap_try([&]() {
            *result = *(int64_t*)((int8_t*)data + offset);
        });
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
    return new file(st.st_size, data);
}

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
