/*
 * Logging File
 * WinDurango.Common::Logging
 */
#include "WinDurango.Common/Interfaces/Storage/File.h"
#include "WinDurango.Common/exports.h"
#include <iostream>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace wd::common
{
    class WD_API Logging
    {
      public:
        Logging() : pFile(nullptr), isConstructed(false)
        {
        }
        Logging(std::shared_ptr<interfaces::storage::File> file) : pFile(file), isConstructed(true)
        {
        }

        void Initialize();

        void AddLogger(std::string codespace);

        /*
         * Example:
         * Log("WinDurango::Common::Storage", "Failed to do smth - Code %s", 5);
         * >> [WinDurango::Common::Storage] 11:25AM 26/12/2025 - Failed to do smth - Code 5
         */
        template <typename... Args> void Log(std::string codespace, fmt::format_string<Args...> fmt, Args &&...args)
        {
            if (!isInitialized)
            {
                std::cout << "[WinDurango::Common::Logging.Initialize] - Critical: Logging isn't Initiailized\n";
                return;
            }
            try
            {
                if (log.find(codespace) != log.end())
                {
                    log[codespace]->info(fmt, std::forward<Args>(args)...);
                }
                else
                {
                    AddLogger(codespace);
                    log[codespace]->info(fmt, std::forward<Args>(args)...);
                }
            }
            catch (const spdlog::spdlog_ex &e)
            {
                std::cout << "[WinDurango::Common::Logging.spdlog] - Critical: " << e.what() << "\n";
            }
            catch (const std::exception &e)
            {
                std::cout << "[WinDurango::Common::Logging.exception] - Critical: " << e.what() << "\n";
            }
            catch (...)
            {
                std::cout << "[WinDurango::Common::Logging.(...))] - Critical: Unknown Error\n";
            }
        }

        template <typename... Args> void Warn(std::string codespace, fmt::format_string<Args...> fmt, Args &&...args)
        {
            if (!isInitialized)
            {
                std::cout << "[WinDurango::Common::Logging.Initialize] - Critical: Logging isn't Initiailized\n";
                return;
            }
            try
            {
                if (log.find(codespace) != log.end())
                {
                    log[codespace]->warn(fmt, std::forward<Args>(args)...);
                }
                else
                {
                    AddLogger(codespace);
                    log[codespace]->warn(fmt, std::forward<Args>(args)...);
                }
            }
            catch (const spdlog::spdlog_ex &e)
            {
                std::cout << "[WinDurango::Common::Logging.spdlog] - Critical: " << e.what() << "\n";
            }
            catch (const std::exception &e)
            {
                std::cout << "[WinDurango::Common::Logging.exception] - Critical: " << e.what() << "\n";
            }
            catch (...)
            {
                std::cout << "[WinDurango::Common::Logging.(...))] - Critical: Unknown Error\n";
            }
        }

        template <typename... Args> void Error(std::string codespace, fmt::format_string<Args...> fmt, Args &&...args)
        {
            if (!isInitialized)
            {
                std::cout << "[WinDurango::Common::Logging.Initialize] - Critical: Logging isn't Initiailized\n";
                return;
            }
            try
            {
                if (log.find(codespace) != log.end())
                {
                    log[codespace]->error(fmt, std::forward<Args>(args)...);
                }
                else
                {
                    AddLogger(codespace);
                    log[codespace]->error(fmt, std::forward<Args>(args)...);
                }
            }
            catch (const spdlog::spdlog_ex &e)
            {
                std::cout << "[WinDurango::Common::Logging.spdlog] - Critical: " << e.what() << "\n";
            }
            catch (const std::exception &e)
            {
                std::cout << "[WinDurango::Common::Logging.exception] - Critical: " << e.what() << "\n";
            }
            catch (...)
            {
                std::cout << "[WinDurango::Common::Logging.(...))] - Critical: Unknown Error\n";
            }
        }

      private:
        std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> log;
        std::shared_ptr<interfaces::storage::File> pFile;

      public:
        bool isConstructed = false;
        bool isInitialized = false;
    };
} // namespace wd::common