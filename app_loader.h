#ifndef APP_LOADER_H
#define APP_LOADER_H

#include "global.hpp"
#include <memory>

class AppLoader
{
    public:
        explicit AppLoader() { reserved_area = std::make_unique<MemoryArea_t>(); }
        void get_reserved_memory_region(std::pair<void *, void *> &range);
        void release_parent_memory_region();

    private:
        std::unique_ptr<MemoryArea_t> reserved_area;
};

#endif