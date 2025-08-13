#include "flexbuffer_cache.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <flatbuffers/flexbuffers.h>
#include <flatbuffers/idl.h>

#include "cata_utility.h"
#include "filesystem.h"
#include "json.h"
#include "mmap_file.h"
#include "options.h"
#include "std_hash_fs_path.h"
#include "zzip.h"

namespace
{

void try_find_and_throw_json_error( TextJsonValue &jv )
{
    if( jv.test_object() ) {
        TextJsonObject jo = jv.get_object();
        for( TextJsonMember jm : jo ) {
            try_find_and_throw_json_error( jm );
        }
    } else if( jv.test_array() ) {
        TextJsonArray ja = jv.get_array();
        for( TextJsonValue jav : ja ) {
            try_find_and_throw_json_error( jav );
        }
    }
}

std::filesystem::file_time_type get_file_mtime_millis( const std::filesystem::path &path,
        std::error_code &ec )
{
    std::filesystem::file_time_type ret = std::filesystem::last_write_time( path, ec );
    if( ec ) {
        return ret;
    }
    // Truncate to nearest millisecond.
    ret = std::filesystem::file_time_type( std::chrono::milliseconds(
            std::chrono::duration_cast<std::chrono::milliseconds>( ret.time_since_epoch() ).count() ) );
    return ret;
}

std::vector<uint8_t> parse_json_to_flexbuffer_(
    const char *buffer,
    const char *source_filename_opt ) noexcept( false )
{
    flatbuffers::IDLOptions opts;
    opts.strict_json = true;
    opts.use_flexbuffers = true;
    opts.no_warnings = true;
    flatbuffers::Parser parser{ opts };
    flexbuffers::Builder fbb;

    if( !parser.ParseFlexBuffer( buffer, source_filename_opt, &fbb ) ) {
        std::istringstream is{ buffer };
        TextJsonIn jsin{ is, source_filename_opt ? source_filename_opt : "<unknown source file>" };

        if( fbb.HasDuplicateKeys() ) {
            // The error from the parser isn't very informative. TextJsonObject can detect the
            // condition but we have to deep scan to find it.
            TextJsonValue jv = jsin.get_value();
            try_find_and_throw_json_error( jv );
            // If we didn't find and throw it, reset the stream.
            is.seekg( 0, std::ios_base::beg );
        }

        size_t line = 0;
        size_t col = 0;
        size_t error_offset = 0;
        if( ( error_offset = parser.error_.find( "EOF:" ) ) == std::string::npos ) {
            // Try to extract line and col to position TextJsonIn at an appropriate location.
            // %n modifier returns number of characters consumed by sscanf to skip the text
            // in the error that the flexbuffer parser returns, because TextJsonIn will add it.
            // Format is "(filename:)?(EOF:|line:col:) message"
            // But filename might be C:something
            int scanned_chars = 0;
            // NOLINTNEXTLINE(cert-err34-c)
            if( sscanf( parser.error_.c_str(), "%*[^:]:%*[^:]:%zu:%zu: %n", &line, &col,
                        &scanned_chars ) != 2 &&
                // NOLINTNEXTLINE(cert-err34-c)
                sscanf( parser.error_.c_str(), "%*[^:]:%zu:%zu: %n", &line, &col, &scanned_chars ) != 2 &&
                // NOLINTNEXTLINE(cert-err34-c)
                sscanf( parser.error_.c_str(), "%zu:%zu: %n", &line, &col, &scanned_chars ) != 2 ) {
                line = 0;
                col = 0;
            }
            error_offset = scanned_chars;
        } else {
            error_offset += 5; // skip "EOF: " because find returns the offset of the start.
        }

        constexpr const char *kErrorPrefix = "error: ";
        if( strncmp( parser.error_.c_str() + error_offset, kErrorPrefix, strlen( kErrorPrefix ) ) == 0 ) {
            error_offset += strlen( kErrorPrefix );
        }

        if( line != 0 ) {
            // Lines are 1 indexed.
            while( line > 1 ) {
                is.ignore( std::numeric_limits<std::streamsize>::max(), '\n' );
                --line;
            }
        } else {
            // Seek to end.
            is.seekg( 0, std::ios_base::end );
            // Force EOF state
            is.peek();
        }
        jsin.error( col ? col - 1 : 0, parser.error_.substr( error_offset ) );
    }

    return std::move( fbb ).GetBuffer();
}

} // namespace

struct flexbuffer_vector_storage : flexbuffer_storage {
    std::vector<uint8_t> buffer_;

    explicit flexbuffer_vector_storage( std::vector<uint8_t> &&buffer ) : buffer_{ std::move( buffer ) } {}

    const uint8_t *data() const override {
        return buffer_.data();
    }

    size_t size() const override {
        return buffer_.size();
    }
};

struct flexbuffer_mmap_storage : flexbuffer_storage {
    std::shared_ptr<const mmap_file> mmap_handle_;

    explicit flexbuffer_mmap_storage( std::shared_ptr<const mmap_file> mmap_handle ) : mmap_handle_{ std::move( mmap_handle ) } {}

    const uint8_t *data() const override {
        return static_cast<const uint8_t *>( mmap_handle_->base() );
    }
    size_t size() const override {
        return mmap_handle_->len();
    }
};

parsed_flexbuffer::parsed_flexbuffer( std::shared_ptr<flexbuffer_storage> storage )
    : storage_{ std::move( storage ) }
{
    // TODO assert?
}

struct file_flexbuffer : parsed_flexbuffer {
        file_flexbuffer(
            std::shared_ptr<flexbuffer_storage> &&storage,
            std::filesystem::path &&source_file_path,
            std::filesystem::file_time_type mtime,
            size_t offset )
            : parsed_flexbuffer( std::move( storage ) ),
              source_file_path_{ std::move( source_file_path ) },
              mtime_{ mtime },
              offset_{ static_cast<std::streampos>( offset ) } {}

        ~file_flexbuffer() override = default;

        bool is_stale() const override {
            std::error_code ec;
            std::filesystem::file_time_type mtime = get_file_mtime_millis( source_file_path_, ec );
            if( ec ) {
                // Assume yes out of date.
                return true;
            }
            return mtime != mtime_;
        }

        std::unique_ptr<std::istream> get_source_stream() const noexcept( false ) override {
            std::unique_ptr<std::istream> ifs = read_maybe_compressed_file( source_file_path_ );
            if( !ifs || !ifs->good() ) {
                return nullptr;
            }
            ifs->seekg( offset_ );
            return ifs;
        }

        std::filesystem::path get_source_path() const noexcept override {
            return source_file_path_;
        }

    private:
        std::filesystem::path source_file_path_;
        std::filesystem::file_time_type mtime_;
        std::streampos offset_;
};

struct string_flexbuffer : parsed_flexbuffer {
        string_flexbuffer( std::shared_ptr<flexbuffer_storage> &&storage, std::string &&source )
            : parsed_flexbuffer{ std::move( storage ) },
              source_{ std::move( source ) } {}

        ~string_flexbuffer() override = default;

        bool is_stale() const override {
            return false;
        }

        std::unique_ptr<std::istream> get_source_stream() const override {
            // This makes a copy but it should only be called in error conditions, and
            // strings parsed to json at runtime tend to be short anyway.
            return std::make_unique<std::istringstream>( source_ );
        }

        std::filesystem::path get_source_path() const noexcept override {
            return {};
        }

    private:
        std::string source_;
};

class flexbuffer_disk_cache
{
    public:
        static std::unique_ptr<flexbuffer_disk_cache> init_from_folder( const std::filesystem::path
                &cache_path,
                const std::filesystem::path &root_path ) {
            // Private constructor, make_unique doesn't have access.
            std::unique_ptr<flexbuffer_disk_cache> cache{ new flexbuffer_disk_cache( cache_path, root_path ) };

            std::filesystem::path cache_zzip_path = cache_path;
            cache_zzip_path.concat( ".zzip" );

            assure_dir_exist( cache_zzip_path.parent_path() );

            std::shared_ptr<zzip> cache_zzip = zzip::load( cache_zzip_path,
                                               ( PATH_INFO::compression_folder_path() / "flexbuffers.dict" ).get_unrelative_path() );
            if( !cache_zzip ) {
                return nullptr;
            }

            std::vector<std::filesystem::path> all_cached_flexbuffers = cache_zzip->get_entries();
            std::unordered_set<std::filesystem::path, std_fs_path_hash> deletions;

            for( const std::filesystem::path &cached_flexbuffer_path : all_cached_flexbuffers ) {
                // The file path format is <input file>.<mtime>.fb
                // So find the second-to-last-dot
                std::filesystem::path json_with_mtime = cached_flexbuffer_path.stem();
                std::string mtime_str = json_with_mtime.extension().generic_u8string();
                std::filesystem::path original_json_file_name = json_with_mtime.stem();
                if( mtime_str.empty() || original_json_file_name.empty() ) {
                    // Not a recognized flexbuffer filename.
                    deletions.emplace( cached_flexbuffer_path );
                    continue;
                }

                // Explicit constructor from milliseconds to file_time_type.
                std::filesystem::file_time_type cached_mtime = std::filesystem::file_time_type(
                            std::chrono::milliseconds( std::strtoull(
                                    // extension() returns a string with the leading .
                                    mtime_str.c_str() + 1,
                                    nullptr, 0 ) ) );

                std::string root_relative_json_path_string = ( cached_flexbuffer_path.parent_path() /
                        original_json_file_name ).generic_u8string();

                // Don't just blindly insert, we may end up in a situation with multiple flexbuffers for the same input json
                auto old_entry = cache->cached_flexbuffers_.find( root_relative_json_path_string );
                if( old_entry != cache->cached_flexbuffers_.end() ) {
                    if( old_entry->second.mtime < cached_mtime ) {
                        // Lets only keep the newest.
                        deletions.emplace( old_entry->second.flexbuffer_path );
                        old_entry->second = disk_cache_entry{ cached_flexbuffer_path, cached_mtime };
                    } else {
                        // The file we're iterating is older than the cached entry.
                        deletions.emplace( cached_flexbuffer_path );
                    }

                } else {
                    cache->cached_flexbuffers_.emplace( root_relative_json_path_string, disk_cache_entry{ cached_flexbuffer_path, cached_mtime } );
                }
            }
            cache_zzip->delete_files( deletions );
            cache_zzip->compact( 1.0 );
            cache->cache_zzip_ = std::move( cache_zzip );
            return cache;
        }

        std::filesystem::file_time_type cached_mtime_for_json( const std::filesystem::path
                &json_source_path ) {
            auto it = cached_flexbuffers_.find( json_source_path.generic_u8string() );
            if( it != cached_flexbuffers_.end() ) {
                return it->second.mtime;
            }
            return {};
        }

        std::shared_ptr<flexbuffer_vector_storage> load_flexbuffer_if_not_stale(
            const std::filesystem::path &lexically_normal_json_source_path ) {
            std::shared_ptr<flexbuffer_vector_storage> storage;

            std::filesystem::path root_relative_source_path =
                lexically_normal_json_source_path.lexically_relative(
                    root_path_ ).lexically_normal().generic_u8string();

            // Is there even a potential cached flexbuffer for this file.
            auto disk_entry = cached_flexbuffers_.find( root_relative_source_path.generic_u8string() );
            if( disk_entry == cached_flexbuffers_.end() ) {
                return storage;
            }

            std::error_code ec;
            std::filesystem::file_time_type source_mtime = get_file_mtime_millis(
                        lexically_normal_json_source_path, ec );
            if( ec ) {
                return storage;
            }

            const std::filesystem::path &entry_path = disk_entry->second.flexbuffer_path;

            // Does the source file's mtime match what we cached previously
            if( source_mtime != disk_entry->second.mtime ) {
                std::string filepath_and_name = disk_entry->first;
                // we use this as an exclusion condition. Configuration options can be changed all the time, we don't want to warn over those. Same for achievements.
                bool stale_game_data = *root_relative_source_path.begin() != std::filesystem::u8path( "config" ) &&
                                       *root_relative_source_path.begin() != std::filesystem::u8path( "achievements" ) &&
                                       *root_relative_source_path.begin() != std::filesystem::u8path( "templates" );
                if( stale_game_data ) {
                    if( get_option<bool>( "WARN_ON_MODIFIED" ) ) {
                        debugmsg( "Stale game data detected at %s, did you overwrite old files?  When updating the game you must install to a fresh folder, overwriting old files will cause errors.",
                                  filepath_and_name );
                    } else {
                        // we still log the modification warning even if the option is disabled, for sifting bug reports
                        DebugLog( D_WARNING, D_MAIN ) << "Stale game data detected (error disabled by user): " <<
                                                      filepath_and_name;
                    }
                }
                // Cached flexbuffer on disk is out of date, remove it.
                cache_zzip_->delete_files( { entry_path } );
                cached_flexbuffers_.erase( disk_entry );
                return storage;
            }

            size_t file_size = cache_zzip_->get_file_size( entry_path );
            if( file_size == 0 ) {
                return storage;
            }
            std::vector<uint8_t> flexbuffer_binary;
            flexbuffer_binary.resize( file_size );
            if( cache_zzip_->get_file_to( entry_path, reinterpret_cast<std::byte *>( flexbuffer_binary.data() ),
                                          file_size ) != file_size ) {
                return storage;
            }

            storage = std::make_shared<flexbuffer_vector_storage>( std::move( flexbuffer_binary ) );

            return storage;
        }

        bool save_to_disk( const std::filesystem::path &lexically_normal_json_source_path,
                           const std::vector<uint8_t> &flexbuffer_binary ) {
            std::error_code ec;
            std::string json_source_path_string = lexically_normal_json_source_path.generic_u8string();
            std::filesystem::file_time_type mtime = get_file_mtime_millis( lexically_normal_json_source_path,
                                                    ec );
            if( ec ) {
                return false;
            }

            int64_t mtime_ms = std::chrono::duration_cast<std::chrono::milliseconds>
                               ( mtime.time_since_epoch() ).count();

            // Doing some variable reuse to avoid copies.
            std::filesystem::path root_relative_source_path =
                lexically_normal_json_source_path.lexically_relative( root_path_ );
            std::filesystem::path zzip_path = root_relative_source_path;
            zzip_path += std::filesystem::u8path( "." + std::to_string( mtime_ms ) + ".fb" );

            if( !cache_zzip_->add_file( zzip_path, { reinterpret_cast<const char *>( flexbuffer_binary.data() ), flexbuffer_binary.size() } ) ) {
                return false;
            }

            cached_flexbuffers_[root_relative_source_path.generic_u8string()] = disk_cache_entry{ zzip_path, mtime};

            return true;
        }

    private:
        explicit flexbuffer_disk_cache( std::filesystem::path cache_path,
                                        std::filesystem::path root_path ) : cache_path_{ std::move( cache_path ) },
            root_path_{ std::move( root_path ) } {}

        std::filesystem::path cache_path_;
        std::filesystem::path root_path_;

        struct disk_cache_entry {
            std::filesystem::path flexbuffer_path;
            std::filesystem::file_time_type mtime;
        };
        // Maps game root relative json source path to the most recent cached flexbuffer we have on disk for it.
        std::unordered_map<std::string, disk_cache_entry> cached_flexbuffers_;
        std::shared_ptr<zzip> cache_zzip_;
};

flexbuffer_cache::flexbuffer_cache( const std::filesystem::path &cache_directory,
                                    const std::filesystem::path &root_directory )
{
    if( !cache_directory.empty() ) {
        disk_cache_ = flexbuffer_disk_cache::init_from_folder( cache_directory, root_directory );
    }
}

flexbuffer_cache::~flexbuffer_cache() = default;

std::shared_ptr<parsed_flexbuffer> flexbuffer_cache::parse( std::filesystem::path json_source_path,
        size_t offset )
{
    std::string json_source_path_string = json_source_path.generic_u8string();
    std::shared_ptr<const mmap_file> json_source = mmap_file::map_file( json_source_path );
    if( !json_source ) {
        throw std::runtime_error( "Failed to mmap " + json_source_path_string );
    }

    const char *json_text = reinterpret_cast<const char *>( json_source->base() ) + offset;

    std::vector<uint8_t> fb = parse_json_to_flexbuffer_( json_text, json_source_path_string.c_str() );

    auto storage = std::make_shared<flexbuffer_vector_storage>( std::move( fb ) );

    std::error_code ec;
    // If we got this far we can get the mtime.
    std::filesystem::file_time_type mtime = get_file_mtime_millis( json_source_path, ec );
    ( void )ec;

    return std::make_shared<file_flexbuffer>(
               std::move( storage ),
               std::move( json_source_path ),
               mtime,
               offset );
}

std::shared_ptr<parsed_flexbuffer> flexbuffer_cache::parse_and_cache(
    std::filesystem::path lexically_normal_json_source_path, size_t offset )
{
    // Is our cache potentially stale?
    if( disk_cache_ ) {
        std::shared_ptr<flexbuffer_vector_storage> cached_storage =
            disk_cache_->load_flexbuffer_if_not_stale(
                lexically_normal_json_source_path );
        if( cached_storage ) {
            std::error_code ec;
            std::filesystem::file_time_type mtime = get_file_mtime_millis( lexically_normal_json_source_path,
                                                    ec );
            ( void )ec;

            return std::make_shared<file_flexbuffer>( std::move( cached_storage ),
                    std::move( lexically_normal_json_source_path ), mtime, offset );
        }
    }

    std::string json_source_path_string = lexically_normal_json_source_path.generic_u8string();
    std::optional<std::string> json_file_contents = read_whole_file(
                lexically_normal_json_source_path );
    if( !json_file_contents.has_value() || json_file_contents->empty() ) {
        throw std::runtime_error( "Failed to read " + json_source_path_string );
    }
    std::string &json_source = *json_file_contents;

    const char *json_text = reinterpret_cast<const char *>( json_source.c_str() ) + offset;
    std::vector<uint8_t> fb = parse_json_to_flexbuffer_( json_text, json_source_path_string.c_str() );

    if( disk_cache_ ) {
        disk_cache_->save_to_disk( lexically_normal_json_source_path, fb );
    }

    auto storage = std::make_shared<flexbuffer_vector_storage>( std::move( fb ) );

    std::error_code ec;
    std::filesystem::file_time_type mtime = std::filesystem::last_write_time(
            lexically_normal_json_source_path, ec );
    if( ec ) {
        // Whatever.
    }

    return std::make_shared<file_flexbuffer>( std::move( storage ),
            std::move( lexically_normal_json_source_path ),
            mtime, offset );
}

std::shared_ptr<parsed_flexbuffer> flexbuffer_cache::parse_buffer( std::string buffer )
{
    std::vector<uint8_t> fb = parse_json_to_flexbuffer_( buffer.c_str(), nullptr );
    auto storage = std::make_shared<flexbuffer_vector_storage>( std::move( fb ) );
    return std::make_shared<string_flexbuffer>( std::move( storage ), std::move( buffer ) );
}
