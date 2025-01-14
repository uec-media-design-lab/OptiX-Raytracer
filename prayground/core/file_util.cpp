#include "file_util.h"
#include <optional>
#include <array>
#include <prayground/core/util.h>

namespace prayground {

    namespace fs = std::filesystem;

    namespace {
        // Application directory
        fs::path app_dir = fs::path("");
        std::vector<fs::path> search_dirs;

    } // nonamed namespace

    fs::path pgGetExecutableDir()
    {
#if defined(_MSC_VER)
        char path[FILENAME_MAX] = { 0 };
        GetModuleFileName(nullptr, path, FILENAME_MAX);
        return fs::path(path).parent_path().string();
#else
        char path[FILENAME_MAX];
        ssize_t count = readlink("/proc/self/exe", path, FILENAME_MAX);
        return fs::path(std::string(path, (count > 0) ? count : 0)).parent_path().string();
#endif
    }

    // -------------------------------------------------------------------------------
    std::optional<fs::path> pgFindDataPath( const fs::path& relative_path )
    {
        std::vector<std::string> parent_dirs = {
            pgAppDir().string(),
            pgPathJoin(pgAppDir(), "data").string(),
            "",
            pgRootDir().string(),
            pgGetExecutableDir().string()
        };

        for (auto& dir : search_dirs)
            parent_dirs.push_back(dir.string());

        for (auto &parent : parent_dirs)
        {
            auto filepath = pgPathJoin(parent, relative_path);
            if ( fs::exists(filepath) )
                return filepath;
        }
        return std::nullopt;
    }

    // -------------------------------------------------------------------------------
    fs::path pgRootDir() {
        return fs::path(PRAYGROUND_DIR);
    }

    void pgSetAppDir(const fs::path& dir)
    {
        app_dir = dir;
    }

    fs::path pgAppDir()
    {
        return app_dir;
    }

    void pgAddSearchDir(const fs::path& dir)
    {
        search_dirs.push_back(dir);
    }

    // -------------------------------------------------------------------------------
    std::string pgGetExtension( const fs::path& filepath )
    {
        return filepath.has_extension() ? filepath.extension().string() : "";
    }

    std::string pgGetStem(const fs::path& filepath, bool is_dir)
    {
        std::string dirpath = filepath.has_parent_path() ? filepath.parent_path().string() : "";
        std::string stem = filepath.stem().string();
        return is_dir ? dirpath + "/" + stem : stem;
    }

    fs::path pgGetFilename(const fs::path& filepath)
    {
        return filepath.has_filename() ? filepath.filename().string() : "";
    }

    fs::path pgGetDir(const fs::path& filepath)
    {
        return filepath.has_parent_path() ? filepath.parent_path() : "";
    }

    // -------------------------------------------------------------------------------
    void pgCreateDir( const fs::path& abs_path )
    {
        // Check if the directory is existed.
        if (fs::exists(abs_path)) {
            pgLogWarn("The directory '", abs_path, "' is already existed.");
            return;
        }
        // Create new directory with path specified.
        bool result = fs::create_directory(abs_path);
        ASSERT(result, "Failed to create directory '" + abs_path.string() + "'.");
    }

    // -------------------------------------------------------------------------------
    void pgCreateDirs( const fs::path& abs_path )
    {
        // Check if the directory is existed.
        if (fs::exists(abs_path)) {
            pgLogWarn("The directory '", abs_path, "' is already existed.");
            return;
        }
        bool result = fs::create_directories( abs_path );
        ASSERT(result, "Failed to create directories '" + abs_path.string() + "'.");
    }

    // -------------------------------------------------------------------------------
    std::string pgGetTextFromFile(const fs::path& relative_path)
    {
        std::optional<fs::path> filepath = pgFindDataPath(relative_path);
        ASSERT(filepath, "A text file with the path '" + relative_path.string() + "' is not found.");

        std::ifstream file_stream; 
        try
        {
            file_stream.open(filepath.value());
            std::stringstream content_stream;
            content_stream << file_stream.rdbuf();
            file_stream.close();
            return content_stream.str();
        }
        catch(const std::istream::failure& e)
        {
            pgLogFatal("Failed to load text file due to '" + std::string(e.what()) + "'.");
            return "";
        }
    }

} // namespace prayground