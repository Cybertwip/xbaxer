#include "interfaces/Storage/File.h"

namespace wd::impl::winrt::interfaces::storage
{
    bool WinRTFile::open()
    {
        if (file != nullptr)
        {
            return false;
        }
        try
        {
            StorageFolder sf = ApplicationData::Current().LocalFolder();

            for (const auto &part : path.parent_path())
            {
                hstring partStr = hstring(part.wstring());
                sf = sf.GetFolderAsync(partStr).get();
            }
            if (!sf)
            {
                return false;
            }

            StorageFile npFile = sf.GetFileAsync(hstring(path.filename().wstring())).get();
            file = npFile;
            if (!npFile)
            {
                return false;
            }
        }
        catch (const hresult_error &ex)
        {
            return false;
        }
        return false;
    }

    bool WinRTFile::create()
    {
        if (file != nullptr)
        {
            return false;
        }
        try
        {
            StorageFolder sf = ApplicationData::Current().LocalFolder();

            for (const auto &part : path.parent_path())
            {
                hstring partStr = hstring(part.wstring());
                sf = sf.GetFolderAsync(partStr).get();
            }
            if (!sf)
            {
                return false;
            }

            file = sf.CreateFileAsync(hstring(path.filename().wstring()), CreationCollisionOption::OpenIfExists).get();
            if (!file)
            {
                return false;
            }
            return true;
        }
        catch (const hresult_error &ex)
        {
            return false;
        }
        return false;
    }
    /*
     * btw these docs are pretty useful
     * https://learn.microsoft.com/en-us/windows/uwp/get-started/fileio-learning-track
     */
    std::string WinRTFile::read()
    {
        if (file == nullptr)
        {
            return "Failed";
        }
        try
        {
            return to_string(FileIO::ReadTextAsync(file).get());
        }
        catch (const hresult_error &ex)
        {
            return "Failed";
        }
        return "Failed";
    }

    void WinRTFile::operator<<(std::string data)
    {
        if (file == nullptr)
        {
            return;
        }
        try
        {
            FileIO::WriteTextAsync(file, to_hstring(data)).get();
        }
        catch (const hresult_error &ex)
        {
            return;
        }
    }

    bool WinRTFile::close()
    {
        if (file == nullptr)
        {
            return false;
        }
        file = nullptr;
        return false;
    }

    std::filesystem::path WinRTFile::filepath()
    {
        return path;
    }

    std::filesystem::path WinRTFile::fullfilepath()
    {
        StorageFolder sf = ApplicationData::Current().LocalFolder();
        std::filesystem::path rootPath{sf.Path().c_str()};

        return rootPath / path;
    }

    bool WinRTFile::rename(std::string name)
    {
        if (file == nullptr)
        {
            return false;
        }
        try
        {
            file.RenameAsync(to_hstring(name), NameCollisionOption::GenerateUniqueName).get();
            path.replace_filename(name);
            return true;
        }
        catch (const hresult_error &ex)
        {
            return false;
        }
        return false;
    }

    bool WinRTFile::remove()
    {
        if (file == nullptr)
        {
            return false;
        }
        try
        {
            file.DeleteAsync().get();
            return true;
        }
        catch (const hresult_error &ex)
        {
            return false;
        }
        return false;
    }

    bool WinRTFile::move(std::filesystem::path targetPath)
    {
        if (file == nullptr)
        {
            return false;
        }
        try
        {
            StorageFolder target = ApplicationData::Current().LocalFolder();

            for (const auto &part : targetPath.parent_path())
            {
                hstring partStr = hstring(part.wstring());
                target = target.GetFolderAsync(partStr).get();
            }
            if (!target)
            {
                return false;
            }

            file.MoveAsync(target).get();

            std::filesystem::path filename = path.filename();
            path = targetPath;
            path.replace_filename(filename);
            return true;
        }
        catch (const hresult_error &ex)
        {
            return false;
        }
        return false;
    }

    bool WinRTFile::copy(std::filesystem::path targetPath)
    {
        if (file == nullptr)
        {
            return false;
        }
        try
        {
            StorageFolder target = ApplicationData::Current().LocalFolder();

            for (const auto &part : targetPath.parent_path())
            {
                hstring partStr = hstring(part.wstring());
                target = target.GetFolderAsync(partStr).get();
            }
            if (!target)
            {
                return false;
            }

            file.CopyAsync(target).get();
            return true;
        }
        catch (const hresult_error &ex)
        {
            return false;
        }
        return false;
    }
} // namespace wd::impl::winrt::interfaces::storage