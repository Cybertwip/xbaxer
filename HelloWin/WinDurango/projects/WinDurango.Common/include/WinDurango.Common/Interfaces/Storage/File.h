/*
 * wd::common::interfaces::storage::File
 */
#pragma once
#include <filesystem>
#include <istream>

namespace wd::common::interfaces::storage
{
    // TODO should we have separate project for interfaces?

    /** Interface for storage management, to be impl'd for uwp and crossplat */
    class File
    {
      public:
        File()
        {
        }

        virtual bool open() = 0;
        virtual bool create() = 0;
        virtual std::string read() = 0;
        virtual void operator<<(std::string data) = 0; // write
        virtual bool close() = 0;

        virtual std::filesystem::path filepath() = 0;
        virtual std::filesystem::path fullfilepath() = 0;

        virtual bool rename(std::string) = 0;
        virtual bool remove() = 0;
        virtual bool move(std::filesystem::path path) = 0;
        virtual bool copy(std::filesystem::path path) = 0;
    };
} // namespace wd::common::interfaces::storage