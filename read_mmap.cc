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
#include <stdint.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

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
    int64_t read(size_t offset) {
        // Out of bounds check
        assert(offset <= size - sizeof(int64_t));

        return *(int64_t*)((int8_t*)data + offset);
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
        // Print out the number
        std::cout << f->read(offset) << std::endl;
    }

    delete f;

    return 0;
}
