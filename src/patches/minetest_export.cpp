#include "lsqecc/patches/minetest_export.hpp"
#include <sqlite3.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <map>
#include <tuple>
#include <cmath>
#include <cstring>
#include <zstd.h>

namespace lsqecc {

namespace {
    // This struct and vector define the global mapping used throughout the code.
    struct Mapping {
        int id;
        std::string name;
    };

    // Centralized mapping definition.
    const std::vector<Mapping> mappings_list = {
        {0, "air"},
        {1, "latticesurgery:qubit_000011"},
        {2, "latticesurgery:qubit_000111"},
        {3, "latticesurgery:qubit_001011"},
        {4, "latticesurgery:qubit_001111"},
        {5, "latticesurgery:qubit_010011"},
        {6, "latticesurgery:qubit_010111"},
        {7, "latticesurgery:qubit_011011"},
        {8, "latticesurgery:qubit_011111"},
        {9, "latticesurgery:qubit_100011"},
        {10, "latticesurgery:qubit_100111"},
        {11, "latticesurgery:qubit_101011"},
        {12, "latticesurgery:qubit_101111"},
        {13, "latticesurgery:qubit_110011"},
        {14, "latticesurgery:qubit_110111"},
        {15, "latticesurgery:qubit_111011"},
        {16, "latticesurgery:qubit_111111"},
        {17, "latticesurgery:distillation_000000"},
        {18, "latticesurgery:distillation_000100"},
        {19, "latticesurgery:distillation_001000"},
        {20, "latticesurgery:distillation_001100"},
        {21, "latticesurgery:distillation_010000"},
        {22, "latticesurgery:distillation_010100"},
        {23, "latticesurgery:distillation_011000"},
        {24, "latticesurgery:distillation_011100"},
        {25, "latticesurgery:distillation_100000"},
        {26, "latticesurgery:distillation_100100"},
        {27, "latticesurgery:distillation_101000"},
        {28, "latticesurgery:distillation_101100"},
        {29, "latticesurgery:distillation_110000"},
        {30, "latticesurgery:distillation_110100"},
        {31, "latticesurgery:distillation_111000"},
        {32, "latticesurgery:distillation_111100"},
        {33, "latticesurgery:routing_1_000000"},
        {34, "latticesurgery:routing_1_000100"},
        {35, "latticesurgery:routing_1_001000"},
        {36, "latticesurgery:routing_1_001100"},
        {37, "latticesurgery:routing_1_010000"},
        {38, "latticesurgery:routing_1_010100"},
        {39, "latticesurgery:routing_1_011000"},
        {40, "latticesurgery:routing_1_011100"},
        {41, "latticesurgery:routing_1_100000"},
        {42, "latticesurgery:routing_1_100100"},
        {43, "latticesurgery:routing_1_101000"},
        {44, "latticesurgery:routing_1_101100"},
        {45, "latticesurgery:routing_1_110000"},
        {46, "latticesurgery:routing_1_110100"},
        {47, "latticesurgery:routing_1_111000"},
        {48, "latticesurgery:routing_1_111100"}
    };

    // This function constructs a map from material name to its corresponding id.
    const std::map<std::string, int>& getNameToIdMap() {
        static std::map<std::string, int> nameToId;
        if (nameToId.empty()) {
            for (const auto& m : mappings_list) {
                nameToId[m.name] = m.id;
            }
        }
        return nameToId;
    }
} // anonymous namespace

// --- Helper Types and Functions ---

using PatternKey = std::tuple<int, int, int>; // (x, y, z)

// construct_mapblock_param0_for_region builds a 4096-node (16x16x16) param0 binary array
// for the specified mapBlock using the pattern data. Unspecified nodes remain 0 (air).
static std::string construct_mapblock_param0_for_region(int mb_x, int mb_y, int mb_z,
                                                         const std::map<PatternKey, int>& pattern)
{
    const int total_nodes = 16 * 16 * 16;
    std::vector<uint16_t> node_ids(total_nodes, 0); // default air (0)
    for (const auto& entry : pattern) {
        int gx, gy, gz;
        std::tie(gx, gy, gz) = entry.first;
        int block_x = gx / 16;
        int block_y = gy / 16;
        int block_z = gz / 16;
        if (block_x == mb_x && block_y == mb_y && block_z == mb_z) {
            int local_x = gx % 16;
            int local_y = gy % 16;
            int local_z = gz % 16;
            int index = local_z * (16 * 16) + local_y * 16 + local_x;
            node_ids[index] = entry.second;
        }
    }
    std::string result;
    result.resize(total_nodes * 2, '\0');
    for (int i = 0; i < total_nodes; i++) {
        uint16_t val = node_ids[i];
        result[2 * i] = static_cast<char>((val >> 8) & 0xFF);
        result[2 * i + 1] = static_cast<char>(val & 0xFF);
    }
    return result;
}

// construct_mapblock builds the full mapBlock blob by combining a header, the param0 data,
// two 4096-byte arrays for param1 and param2 (filled with zeros), and trailing bytes.
static std::string construct_mapblock(const std::string& param0)
{
    std::string header;
    header.append("\x00\x00\x00\x00\x00\x00\x00", 7); // flags, lighting, timestamp
    header.push_back('\x00'); // Name-ID Mapping version (set to 0)

    // Use the centralized mappings_list to add mapping data.
    uint16_t num_mappings = static_cast<uint16_t>(mappings_list.size());
    header.push_back(static_cast<char>((num_mappings >> 8) & 0xFF));
    header.push_back(static_cast<char>(num_mappings & 0xFF));

    for (const auto& m : mappings_list) {
        header.push_back(static_cast<char>((m.id >> 8) & 0xFF));
        header.push_back(static_cast<char>(m.id & 0xFF));
        uint16_t name_len = m.name.size();
        header.push_back(static_cast<char>((name_len >> 8) & 0xFF));
        header.push_back(static_cast<char>(name_len & 0xFF));
        header.append(m.name);
    }
    header.push_back('\x02'); // content_width = 2
    header.push_back('\x02'); // params_width = 2

    std::string param1(4096, '\x00');
    std::string param2(4096, '\x00');
    std::string trailing("\x00\x00\x00\x00\n\x00\x00", 7);
    std::string blob = header + param0 + param1 + param2 + trailing;

    return blob;
}

// compress_blob compresses the decompressed mapBlock blob using Zstandard,
// prepends a version byte, and returns the result as a hexadecimal string.
static std::string compress_blob(uint8_t version, const std::string& decompressed_data, int compression_level = 3)
{
    size_t bound = ZSTD_compressBound(decompressed_data.size());
    std::vector<char> compressed(bound);
    size_t compressed_size = ZSTD_compress(compressed.data(), bound,
                                           decompressed_data.data(), decompressed_data.size(),
                                           compression_level);
    if (ZSTD_isError(compressed_size))
        throw std::runtime_error("Compression failed: " + std::string(ZSTD_getErrorName(compressed_size)));
    std::string result;
    result.push_back(static_cast<char>(version));
    result.append(compressed.data(), compressed_size);
    
    static const char* hex_chars = "0123456789ABCDEF";
    std::string hex;
    for (unsigned char c : result) {
        hex.push_back(hex_chars[(c >> 4) & 0xF]);
        hex.push_back(hex_chars[c & 0xF]);
    }
    return hex;
}

// compute_mapblock_position computes a unique integer position from the mapBlock coordinates
static int64_t compute_mapblock_position(int mb_x, int mb_y, int mb_z)
{
    return (static_cast<int64_t>(mb_z) << 24) | (static_cast<int64_t>(mb_y) << 12) | mb_x;
}

// save_to_sqlite inserts or replaces a mapBlock (given by its position and compressed blob, as hex)
// into the SQLite database.
static bool save_to_sqlite(int64_t pos, const std::string& blob_hex, const std::string& db_path)
{
    size_t len = blob_hex.size();
    if (len % 2 != 0) return false;
    std::vector<unsigned char> blob_bytes;
    blob_bytes.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        std::string byte_str = blob_hex.substr(i, 2);
        unsigned char byte = static_cast<unsigned char>(std::stoul(byte_str, nullptr, 16));
        blob_bytes.push_back(byte);
    }
    sqlite3* db;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Cannot open database in save_to_sqlite: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    const char* create_table_sql = R"(
        CREATE TABLE IF NOT EXISTS blocks (
            pos INTEGER PRIMARY KEY,
            data BLOB
        );
    )";
    char* errmsg = nullptr;
    if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::cerr << "SQL error in save_to_sqlite: " << errmsg << "\n";
        sqlite3_free(errmsg);
        sqlite3_close(db);
        return false;
    }
    const char* insert_sql = "INSERT OR REPLACE INTO blocks (pos, data) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement in save_to_sqlite: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return false;
    }
    sqlite3_bind_int64(stmt, 1, pos);
    sqlite3_bind_blob(stmt, 2, blob_bytes.data(), static_cast<int>(blob_bytes.size()), SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Insert failed in save_to_sqlite for pos " << pos << ": " << sqlite3_errmsg(db) << "\n";
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return false;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return true;
}

// load_pattern_from_string now uses the centralized mapping from material name to id.
static std::pair<std::map<PatternKey, int>, std::tuple<int, int, int>>
load_pattern_from_string(const std::string& pattern_str, int stripe_height)
{
    std::istringstream iss(pattern_str);
    std::map<PatternKey, int> pattern;
    int max_x = 0, max_y = 0, max_z = 0;
    std::string line;
    const auto& nameToIdMap = getNameToIdMap();
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        size_t pos1 = line.find('(');
        size_t pos2 = line.find(')');
        if (pos1 == std::string::npos || pos2 == std::string::npos)
            continue; // skip invalid lines
        std::string coords = line.substr(pos1 + 1, pos2 - pos1 - 1);
        std::istringstream coord_stream(coords);
        std::string token;
        std::vector<int> values;
        while (std::getline(coord_stream, token, ',')) {
            values.push_back(std::stoi(token));
        }
        if (values.size() != 3) continue;
        int x = values[0];
        int z = values[1];
        int y = values[2] * stripe_height; // scale y
        std::string material = line.substr(pos2 + 1);
        material.erase(0, material.find_first_not_of(" \t"));
        int mat_id = 0;
        auto it = nameToIdMap.find(material);
        if (it != nameToIdMap.end())
            mat_id = it->second;
        pattern[{x, y, z}] = mat_id;
        if (x > max_x) max_x = x;
        if (y > max_y) max_y = y;
        if (z > max_z) max_z = z;
    }
    return {pattern, {max_x + 1, max_y + 1, max_z + 1}};
}

bool generate_minetest_map(const std::string& pattern_str,
                           const std::string& db_path,
                           int stripe_height)
{
    std::map<PatternKey, int> pattern;
    int global_size_x, global_size_y, global_size_z;
    try {
        auto res = load_pattern_from_string(pattern_str, stripe_height);
        pattern = res.first;
        std::tie(global_size_x, global_size_y, global_size_z) = res.second;
    } catch (const std::exception& ex) {
        std::cerr << "Error loading pattern: " << ex.what() << "\n";
        return false;
    }

    int num_mb_x = (global_size_x + 15) / 16;
    int num_mb_y = (global_size_y + 15) / 16;
    int num_mb_z = (global_size_z + 15) / 16;
    uint8_t version = 29; // Version byte

    for (int mb_y = 0; mb_y < num_mb_y; ++mb_y) {
        for (int mb_x = 0; mb_x < num_mb_x; ++mb_x) {
            for (int mb_z = 0; mb_z < num_mb_z; ++mb_z) {
                std::string param0 = construct_mapblock_param0_for_region(mb_x, mb_y, mb_z, pattern);
                std::string mapblock_blob = construct_mapblock(param0);
                std::string compressed_blob;
                try {
                    compressed_blob = compress_blob(version, mapblock_blob);
                } catch (const std::exception& ex) {
                    std::cerr << "Compression error: " << ex.what() << "\n";
                    return false;
                }
                int64_t pos = compute_mapblock_position(mb_x, mb_y, mb_z);
                if (!save_to_sqlite(pos, compressed_blob, db_path)) {
                    std::cerr << "Failed to save mapBlock at (" << mb_x << "," << mb_y << "," << mb_z << ")\n";
                }
            }
        }
    }
    return true;
}

} // namespace lsqecc
