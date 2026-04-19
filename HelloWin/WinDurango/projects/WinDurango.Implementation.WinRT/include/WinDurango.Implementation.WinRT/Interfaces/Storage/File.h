#pragma once
#include "WinDurango.Common/Interfaces/Storage/File.h"
#include "WinDurango.Implementation.WinRT/WinDurangoImplWinRT.h"
#include <filesystem>
#include <istream>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>

using namespace winrt::Windows::Storage;
using namespace winrt;

namespace wd::impl::winrt::interfaces::storage
{
    class WDIMPL_API WinRTFile : public wd::common::interfaces::storage::File
    {
      public:
        WinRTFile(std::filesystem::path filepath) : path(filepath), file(nullptr)
        {
        }

        virtual bool open() override;
        virtual bool create() override;
        virtual std::string read() override;
        virtual void operator<<(std::string data) override; // write
        virtual bool close() override;

        virtual std::filesystem::path filepath() override;
        virtual std::filesystem::path fullfilepath() override;

        virtual bool rename(std::string) override;
        virtual bool remove() override;
        virtual bool move(std::filesystem::path path) override;
        virtual bool copy(std::filesystem::path path) override;

      private:
        std::filesystem::path path;
        StorageFile file;
    };
} // namespace wd::impl::winrt::interfaces::storage