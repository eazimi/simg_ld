#ifndef APP_LOADER_H
#define APP_LOADER_H

#include "app_loader_global.hpp"
#include <memory>

class AppLoader
{
    public:
        explicit AppLoader() { reserved_area = std::make_unique<MemoryArea_t>(); }
        void getReservedMemRange(std::pair<void *, void *> &range);
        void printMMappedRanges();
        void memUnmapRanges();

    private:
        int readMapsLine(int mapsfd, Area *area);
        std::unique_ptr<MemoryArea_t> reserved_area;
};

#endif