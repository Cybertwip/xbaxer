#include "Interfaces/Storage/Directory.h"

/*
 * This is a Dir Class and all the funcs inside
 * are specifically for managing this dir.
 */
bool wd::impl::winrt::interfaces::storage::WinRTDirectory::open()
{
    if (dir != nullptr)
    {
        return false;
    }
    try
    {
        StorageFolder sf = ApplicationData::Current().LocalFolder();

        for (const auto &part : path)
        {
            hstring partStr = hstring(part.wstring());
            sf = sf.GetFolderAsync(partStr).get();
        }

        if (sf)
        {
            dir = sf;
            return true;
        }
    }
    catch (const hresult_error &ex)
    {
        return false;
    }
    return false;
}

/*
 * Todo:
 * Add logging
 *
 * TargetPath is the filename btw
 * any path will be ignored.
 */
std::shared_ptr<wd::common::interfaces::storage::File> wd::impl::winrt::interfaces::storage::WinRTDirectory::CreateFile(
    std::filesystem::path targetPath)
{
    if (dir == nullptr)
    {
        return nullptr;
    }
    try
    {
        auto file =
            dir.CreateFileAsync(hstring(targetPath.filename().wstring()), CreationCollisionOption::OpenIfExists).get();
        std::shared_ptr<wd::impl::winrt::interfaces::storage::WinRTFile> fileRT =
            std::make_shared<wd::impl::winrt::interfaces::storage::WinRTFile>(path / targetPath.filename());
        return fileRT;
    }
    catch (const hresult_error &ex)
    {
        return nullptr;
    }
}

/*
 * TODO: Use Shared Pointers
 */
std::shared_ptr<wd::common::interfaces::storage::Directory> wd::impl::winrt::interfaces::storage::WinRTDirectory::
    CreateFolder(std::filesystem::path targetPath)
{
    if (dir == nullptr)
    {
        return nullptr;
    }
    try
    {
        auto file =
            dir.CreateFolderAsync(hstring(targetPath.filename().wstring()), CreationCollisionOption::OpenIfExists)
                .get();
        std::shared_ptr<wd::impl::winrt::interfaces::storage::WinRTDirectory> folderRT =
            std::make_shared<wd::impl::winrt::interfaces::storage::WinRTDirectory>(path / targetPath.filename());
        return folderRT;
    }
    catch (const hresult_error &ex)
    {
        return nullptr;
    }
}

std::filesystem::path wd::impl::winrt::interfaces::storage::WinRTDirectory::dirpath()
{
    return path;
}

bool wd::impl::winrt::interfaces::storage::WinRTDirectory::rename(std::string newName)
{
    if (dir == nullptr)
    {
        return false;
    }
    try
    {
        dir.RenameAsync(to_hstring(newName)).get();
        path.replace_filename(newName);
        return true;
    }
    catch (const hresult_error &ex)
    {
        return false;
    }
    return false;
}

bool wd::impl::winrt::interfaces::storage::WinRTDirectory::remove()
{
    if (dir == nullptr)
    {
        return false;
    }
    try
    {
        dir.DeleteAsync().get();
        return true;
    }
    catch (const hresult_error &ex)
    {
        return false;
    }
    return false;
}

bool MoveFolder(StorageFolder source, StorageFolder destinationContainer)
{
    try
    {
        StorageFolder destinationFolder =
            destinationContainer.CreateFolderAsync(source.Name(), CreationCollisionOption::OpenIfExists).get();

        for (auto file : source.GetFilesAsync().get())
        {
            file.MoveAsync(destinationFolder, file.Name(), NameCollisionOption::GenerateUniqueName).get();
        }

        for (auto folder : source.GetFoldersAsync().get())
        {
            MoveFolder(folder, destinationFolder);
        }
        return true;
    }
    catch (const hresult_error &ex)
    {
        return false;
    }
}

bool wd::impl::winrt::interfaces::storage::WinRTDirectory::move(std::filesystem::path targetPath)
{
    if (dir == nullptr)
    {
        return false;
    }
    try
    {
        StorageFolder target = ApplicationData::Current().LocalFolder();

        for (const auto &part : targetPath)
        {
            hstring partStr = hstring(part.wstring());
            target = target.GetFolderAsync(partStr).get();
        }

        if (MoveFolder(dir, target))
        {
            path = targetPath;
            return true;
        }
    }
    catch (const hresult_error &ex)
    {
        return false;
    }
    return false;
}

bool CopyFolder(StorageFolder source, StorageFolder destinationContainer)
{
    try
    {
        StorageFolder destinationFolder =
            destinationContainer.CreateFolderAsync(source.Name(), CreationCollisionOption::OpenIfExists).get();

        for (auto file : source.GetFilesAsync().get())
        {
            file.CopyAsync(destinationFolder, file.Name(), NameCollisionOption::GenerateUniqueName).get();
        }

        for (auto folder : source.GetFoldersAsync().get())
        {
            CopyFolder(folder, destinationFolder);
        }
        return true;
    }
    catch (const hresult_error &ex)
    {
        return false;
    }
}

bool wd::impl::winrt::interfaces::storage::WinRTDirectory::copy(std::filesystem::path targetPath)
{
    if (dir == nullptr)
    {
        return false;
    }
    try
    {
        StorageFolder target = ApplicationData::Current().LocalFolder();

        for (const auto &part : targetPath)
        {
            hstring partStr = hstring(part.wstring());
            target = target.GetFolderAsync(partStr).get();
        }

        if (CopyFolder(dir, target))
        {
            path = targetPath;
            return true;
        }
    }
    catch (const hresult_error &ex)
    {
        return false;
    }
    return false;
}