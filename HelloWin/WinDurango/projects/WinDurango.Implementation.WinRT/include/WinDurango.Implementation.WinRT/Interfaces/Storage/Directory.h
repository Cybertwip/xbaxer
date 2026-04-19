#pragma once
#include "WinDurango.Common/interfaces/Storage/Directory.h"
#include "WinDurango.Implementation.WinRT/Interfaces/Storage/File.h"
#include "WinDurango.Implementation.WinRT/WinDurangoImplWinRT.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>

using namespace winrt::Windows::Storage;
using namespace winrt;

/*
 * I wonder if this is too confusing?
 */
namespace wd::impl::winrt::interfaces::storage
{
    class WDIMPL_API WinRTDirectory : public wd::common::interfaces::storage::Directory
    {
      public:
        WinRTDirectory(std::filesystem::path dirpath) : path(dirpath), dir(nullptr)
        {
        }

        virtual bool open() override;
        virtual std::shared_ptr<wd::common::interfaces::storage::File> CreateFile(std::filesystem::path path) override;
        virtual std::shared_ptr<wd::common::interfaces::storage::Directory> CreateFolder(
            std::filesystem::path path) override;

        virtual std::filesystem::path dirpath() override;

        virtual bool rename(std::string) override;
        virtual bool remove() override;
        virtual bool move(std::filesystem::path path) override;
        virtual bool copy(std::filesystem::path path) override;

      private:
        std::filesystem::path path;
        StorageFolder dir;
    };
} // namespace wd::impl::winrt::interfaces::storage