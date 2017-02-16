// AzureFileConsole.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

namespace AzureFileConsole
{
    using namespace std;
    using namespace utility;
    using namespace azure::storage;

    class AzureFileContext
    {
    public:

        AzureFileContext()
        {
        }

        AzureFileContext(const string_t& account_name, const string_t& account_key)
            : m_account_name(account_name), m_account_key(account_key)
        {
            m_storage_credentials = storage_credentials(m_account_name, m_account_key);
            Init();
        }

        AzureFileContext(const string_t& sas_token)
            : m_sas_token(sas_token)
        {
            m_storage_credentials = storage_credentials(m_sas_token);
            Init();
        }

        cloud_file_client FileClient() const
        {
            return m_file_client;
        }

        const string_t& CurrentUri() const
        {
            return m_current_uri;
        }

        void CurrentUri(const string_t& uri)
        {
            m_current_uri = uri;
        }

        const cloud_file_share& CurrentShare() const
        {
            return m_current_share;
        }

        void CurrentShare(const cloud_file_share& file_share)
        {
            m_current_share = file_share;
        }

        const cloud_file_directory& CurrentDirectory() const
        {
            return m_current_directory;
        }

        void CurrentDirectory(const cloud_file_directory& file_directory)
        {
            m_current_directory = file_directory;
        }

    private:

        void Init()
        {
            m_storage_account = cloud_storage_account(m_storage_credentials, true);
            m_file_client = m_storage_account.create_cloud_file_client();
            m_current_uri = m_file_client.base_uri().primary_uri().to_string();
        }

        string_t m_account_name;
        string_t m_account_key;
        string_t m_sas_token;
        storage_credentials m_storage_credentials;
        cloud_storage_account m_storage_account;
        cloud_file_client m_file_client;

        cloud_file_share m_current_share;
        cloud_file_directory m_current_directory;
        string_t m_current_uri;
    };

    class IFileSystem
    {
    public:
        virtual string_t GetFileName(const string_t& path) = 0;
        virtual bool IsDirectory(const string_t& path) = 0;
    };

    class FileSystem : public IFileSystem
    {
    public:
        string_t GetFileName(const string_t& path)
        {
            throw runtime_error("NotImplemented");
        }

        bool IsDirectory(const string_t& path)
        {
            throw runtime_error("NotImplemented");
        }
    };

    class NtfsFileSystem : public FileSystem
    {
    public:
        string_t GetFileName(const string_t& path)
        {
            size_t index = path.find_last_of(_XPLATSTR('\\'));
            return path.substr(index + 1);
        }

        bool IsDirectory(const string_t& path)
        {
            DWORD dwAttrs = GetFileAttributes(path.c_str());

            if (dwAttrs == INVALID_FILE_ATTRIBUTES)
            {
                return false;
            }

            return ((dwAttrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
        }
    };

    class FileSystemFactory
    {
    public:
        static shared_ptr<IFileSystem> CreateFileSystem()
        {
            return shared_ptr<IFileSystem>(new NtfsFileSystem());
        }
    };

    class ICommand
    {
    public:

        ICommand()
        {
        }

        virtual void PreExecute() = 0;
        virtual void Execute() = 0;
        virtual void PostExecute() = 0;
    };

    class CommandBase : public ICommand
    {
    public:
        CommandBase(const string_t& command, const vector<string_t>& arguments, AzureFileContext& context,  const shared_ptr<IFileSystem>& file_system)
            : m_command(command), m_arguments(arguments), m_context(context), m_file_system(file_system), ICommand()
        {
        }

        void PreExecute()
        {
        }

        void Execute()
        {
        }

        void PostExecute()
        {
        }

    protected:

        string_t m_command_line;
        string_t m_command;
        vector<string_t> m_arguments;

        AzureFileContext& m_context;

        shared_ptr<IFileSystem> m_file_system;
    };

    class DefaultCommand : public CommandBase
    {
    public:
        DefaultCommand(const string_t& command, const vector<string_t>& arguments, AzureFileContext& context, const shared_ptr<IFileSystem>& file_system)
            : CommandBase(command, arguments, context, file_system)
        {
        }
    };

    class DirCommand : public CommandBase
    {
    public:
        DirCommand(const string_t& command, const vector<string_t>& arguments, AzureFileContext& context, const shared_ptr<IFileSystem>& file_system)
            : CommandBase(command, arguments, context, file_system)
        {
        }

        void Execute()
        {
            if (!m_context.CurrentShare().is_valid())
            {
                continuation_token token;

                do
                {
                    share_result_segment result = m_context.FileClient().list_shares_segmented(token);

                    for (auto& item : result.results())
                    {
                        ucout << _XPLATSTR("    ") << item.name() << std::endl;
                    }
                }
                while (!token.empty());
            }
            else
            {
                continuation_token token;

                do
                {
                    list_file_and_directory_result_segment result = m_context.CurrentDirectory().list_files_and_directories_segmented(token);
                    
                    for (auto& item : result.results())
                    {
                        if (item.is_directory())
                        {
                            ucout << _XPLATSTR("<d> ") << item.as_directory().name() << std::endl;
                        }
                        else if (item.is_file())
                        {
                            ucout << _XPLATSTR("    ") << item.as_file().name() << std::endl;
                        }
                    }
                } while (!token.empty());
            }
        }
    };

    class CdCommand : public CommandBase
    {
    public:
        CdCommand(const string_t& command, const vector<string_t>& arguments, AzureFileContext& context, const shared_ptr<IFileSystem>& file_system)
            : CommandBase(command, arguments, context, file_system)
        {
        }

        void PreExecute()
        {
            if (m_arguments.size() == 0)
            {
                throw invalid_argument("Missing arguments");
            }

            string_t directory_name = m_arguments[0];

            if (!m_context.CurrentShare().is_valid())
            {
                if (directory_name.compare(_XPLATSTR(".")) == 0
                    || directory_name.compare(_XPLATSTR("..")) == 0)
                {
                    // share.exists() throws if share_name is "." or ".."
                    throw invalid_argument("Invalid share name");
                }
            }
        }

        void Execute()
        {
            if (!m_context.CurrentShare().is_valid())
            {
                string_t share_name = m_arguments[0];

                cloud_file_share share = m_context.FileClient().get_share_reference(share_name);

                if (share.exists())
                {
                    m_context.CurrentShare(share);
                    m_context.CurrentDirectory(m_context.CurrentShare().get_root_directory_reference());
                    m_context.CurrentUri(m_context.CurrentDirectory().uri().primary_uri().to_string());
                }
            }
            else
            {
                string_t directory_name = m_arguments[0];

                if (directory_name.compare(_XPLATSTR("..")) == 0)
                {
                    string_t currentDirectoryPath = m_context.CurrentDirectory().uri().path();
                    string_t rootDirectoryPath = m_context.CurrentShare().get_root_directory_reference().uri().path();

                    if (currentDirectoryPath.compare(rootDirectoryPath) == 0)
                    {
                        m_context.CurrentShare(cloud_file_share());
                        m_context.CurrentDirectory(cloud_file_directory());
                        m_context.CurrentUri(m_context.FileClient().base_uri().primary_uri().to_string());
                    }
                    else
                    {
                        // Note: The parent_directory of root_directory is still the root_directory
                        m_context.CurrentDirectory(m_context.CurrentDirectory().get_parent_directory_reference());
                        m_context.CurrentUri(m_context.CurrentDirectory().uri().primary_uri().to_string());
                    }
                }
                else if (directory_name.compare(_XPLATSTR(".")) != 0)
                {
                    m_context.CurrentDirectory(m_context.CurrentDirectory().get_subdirectory_reference(directory_name));
                    m_context.CurrentUri(m_context.CurrentDirectory().uri().primary_uri().to_string());
                }
            }
        }
    };

    class UploadCommand : public CommandBase
    {
    public:
        UploadCommand(const string_t& command, const vector<string_t>& arguments, AzureFileContext& context, const shared_ptr<IFileSystem>& file_system)
            : CommandBase(command, arguments, context, file_system)
        {
        }

        void PreExecute()
        {
            if (m_arguments.size() == 0)
            {
                throw invalid_argument("Missing arguments");
            }

            if (!m_context.CurrentShare().is_valid())
            {
                throw invalid_argument("Not in a share root directory");
            }

            string_t path = m_arguments[0];
            if (m_file_system->IsDirectory(path))
            {
                throw invalid_argument("Directory");
            }
        }

        void Execute()
        {
            string_t path = m_arguments[0];
            string_t fileName;

            if (m_arguments.size() > 1)
            {
                fileName = m_arguments[1];
            }
            else
            {
                fileName = m_file_system->GetFileName(path);
            }

            cloud_file file = m_context.CurrentDirectory().get_file_reference(fileName);
            file.upload_from_file(path);
        }
    };

    class CommandFactory
    {
    public:
        static shared_ptr<ICommand> Create(const string_t& command_line, AzureFileContext& context)
        {
            string_t command;
            vector<string_t> arguments;
            istringstream_t iss(command_line);

            if (!iss.eof())
            {
                iss >> command;
            }

            string_t arg;

            while (!iss.eof())
            {
                iss >> arg;
                arguments.push_back(arg);
            }

            shared_ptr<IFileSystem> file_system = FileSystemFactory::CreateFileSystem();

            if (command.compare(_XPLATSTR("dir")) == 0)
            {
                return shared_ptr<ICommand>(new DirCommand(command, arguments, context, file_system));
            }
            else if (command.compare(_XPLATSTR("cd")) == 0)
            {
                return shared_ptr<ICommand>(new CdCommand(command, arguments, context, file_system));
            }
            else if (command.compare(_XPLATSTR("upload")) == 0)
            {
                return shared_ptr<ICommand>(new UploadCommand(command, arguments, context, file_system));
            }
            else
            {
                return shared_ptr<ICommand>(new DefaultCommand(command, arguments, context, file_system));
            }
        }
    };
}

utility::string_t combine_uri_paths(const utility::string_t& str1, const utility::string_t& str2)
{
    utility::string_t result = str1;

    if (result.back() == _XPLATSTR('/'))
    {
        result.erase(result.end());
    }

    if (str2.front() != _XPLATSTR('/'))
    {
        result.append(_XPLATSTR("/"));
    }
    
    result.append(str2);
    return result;
}

int main(int argc, const char *argv[])
{
    if (argc == 1)
    {
        ucout << _XPLATSTR("Not enough arguments") << std::endl;
        ucout << _XPLATSTR("Usage:") << std::endl;
        ucout << _XPLATSTR("  ") << argv[0] << _XPLATSTR(" [AccountName] [AccountKey]") << std::endl;
        ucout << _XPLATSTR("  ") << argv[0] << _XPLATSTR(" [SAS Key]") << std::endl;
        return -1;
    }

    AzureFileConsole::AzureFileContext context;

    if (argc == 2)
    {
        utility::stringstream_t token;
        token << argv[1];
        context = AzureFileConsole::AzureFileContext(token.str());
    }
    else
    {
        utility::stringstream_t account;
        utility::stringstream_t key;
        account << argv[1];
        key << argv[2];
        context = AzureFileConsole::AzureFileContext(account.str(), key.str());
    }

    utility::string_t input;

    try
    {
        while (true)
        {
            try
            {
                ucout << std::endl << _XPLATSTR(">>") << context.CurrentUri() << std::endl;
                ucout << _XPLATSTR(">");
                std::getline(ucin, input);

                if (input.compare(_XPLATSTR("exit")) == 0)
                {
                    break;
                }

                std::shared_ptr<AzureFileConsole::ICommand> command = AzureFileConsole::CommandFactory::Create(input, context);
                command->PreExecute();
                command->Execute();
                command->PostExecute();
            }
            catch (const std::exception& e)
            {
                ucout << e.what() << std::endl;
            }
        }
    }
    catch (...)
    {
        ucout << _XPLATSTR("Exit unexpected") << std::endl;
    }

    return 0;
}