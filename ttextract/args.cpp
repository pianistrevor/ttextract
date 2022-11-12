#include <unordered_map>
#include <stdexcept>
#include <iostream>
#include "ttextract.h"

const std::unordered_map<std::string_view, argType> validArgs = {
    {"-h", argType::HELP},      {"--help", argType::HELP},
    {"-u", argType::UNPACK},    {"--unpack", argType::UNPACK},
    {"-s", argType::SIZE},      {"--size", argType::SIZE},
    {"-p", argType::PACKED},    {"--packed", argType::PACKED},
    {"-a", argType::ALG},       {"--alg", argType::ALG},
    {"-r", argType::RAW},       {"--raw", argType::RAW},
    {"-d", argType::DIRECTORY}, {"--directory", argType::DIRECTORY},
    {"-o", argType::OUTFILE},   {"--out", argType::OUTFILE} };

const std::unordered_map<std::string_view, algType> validAlgs = {
    {"none", algType::NONE},
    {"0", algType::NONE},
    {"lz2k", algType::LZ2K},
    {"2", algType::LZ2K} };

std::string stripExt(std::string fileName) {
    auto pivot = fileName.rfind('.');
    if (pivot == std::string::npos) {
        pivot = fileName.length();
    }
    return fileName.substr(0, pivot);
}

void printHelpMessage() {
    std::cout << "usage: ttextract.exe <filename> [options]\n"
        << "  <filename>         Absolute or relative path to file.\n\n"
        << "Archive options (.DAT, .FPK, .PAK files):\n"
        << "  -d, --directory    Directory name for output files. Defaults to file name without extension.\n"
        << "  -r, --raw          Extract raw files, do not unpack compressed files in archive.\n\n"
        << "Single file options:\n"
        << "  -u, --unpack       (REQUIRED) Indicates the file is a compressed file rather than an archive.\n"
        << "  -s, --size         Size of extracted file. If not specified, program will not check output size.\n"
        << "  -p, --packed       Size of source file. If not specified, program will read until end of file.\n"
        << "  -a, --alg          Compression algorithm of source file. If not specified, it will be determined automatically.\n"
        << "  Algorithm options:\n"
        << "      0, none        No compression algorithm (outputs as-is)\n"
        << "      2, lz2k        LZ2K compression algorithm\n"
        << "  -o, --out          Output file name. Defaults to file name with \".dec\" appended.\n\n";
}

void logWarning(std::string message) {
    std::cout << "[WARNING] " << message << '\n';
}

void logError(std::string message) {
    std::cerr << "[ERROR] " << message << '\n';
}

cmdlineArgs parseArgs(int argc, char* argv[]) {
    if (argc == 1) {
        printHelpMessage();
        throw 0; // normal exit
    }
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    cmdlineArgs results;
    // First argument is ALWAYS filename.
    results.fileName = args[0];
    for (int i = 1; i < args.size(); ++i) {
        auto& arg = args[i];
        std::string_view value;
        argType ref;
        try {
            ref = validArgs.at(arg);
        }
        catch (std::out_of_range error) {
            logError("Unrecognized option \"" + static_cast<std::string>(arg) + '\"');
            throw 1;
        }
        switch (ref) {
            using enum argType;
        case HELP:
            printHelpMessage();
            throw 0; // normal exit
        case UNPACK:
            results.isArchive = false;
            results.isUnpack = true;
            break;
        case SIZE:
            if (!results.isUnpack) {
                logError("Option \"" + static_cast<std::string>(arg) + "\" requires -u or --unpack");
                throw 1;
            }
            if (results.size != -1) {
                // Already have a size
                logError("Only one size can be specified.");
                throw 1;
            }
            value = args[++i];
            try {
                results.size = std::stol(static_cast<std::string>(value), nullptr, 0);
            }
            catch (std::invalid_argument) {
                logWarning("Ignoring size - invalid argument");
                results.size = -1;
                break;
            }
            if (results.size < 0) {
                logWarning("Ignoring size - invalid argument");
                results.size = -1;
                break;
            }
            break;
        case PACKED:
            if (!results.isUnpack) {
                logError("Option \"" + static_cast<std::string>(arg) + "\" requires -u or --unpack");
                throw 1;
            }
            if (results.packedSize != -1) {
                // Already have a packed size
                logError("Only one packed size can be specified.");
                throw 1;
            }
            value = args[++i];
            try {
                results.packedSize = std::stol(static_cast<std::string>(value), nullptr, 0);
            }
            catch (std::invalid_argument) {
                logWarning("Ignoring packed size - invalid argument");
                results.packedSize = -1;
                break;
            }
            if (results.packedSize < 0) {
                logWarning("Ignoring packed size - invalid argument");
                results.packedSize = -1;
                break;
            }
            break;
        case ALG:
            if (!results.isUnpack) {
                logError("Option \"" + static_cast<std::string>(arg) + "\" requires -u or --unpack");
                throw 1;
            }
            if (results.alg != algType::UNSPECIFIED) {
                // Already have an algorithm
                logError("Only one algorithm can be specified.");
                throw 1;
            }
            value = args[++i];
            try {
                algType type = validAlgs.at(value);
                results.alg = type;
            }
            catch (std::out_of_range error) {
                logError("Unrecognized algorithm \"" + static_cast<std::string>(value) + '\"');
                throw 1;
            }
            break;
        case RAW:
            if (results.isUnpack) {
                logError("Option \"" + static_cast<std::string>(arg) + "\" incompatible with -u or --unpack");
                throw 1;
            }
            results.isRaw = true;
            break;
        case DIRECTORY:
            if (results.isUnpack) {
                logError("Option \"" + static_cast<std::string>(arg) + "\" incompatible with -u or --unpack");
                throw 1;
            }
            value = args[++i];
            results.outDir = value;
            break;
        case OUTFILE:
            if (!results.isUnpack) {
                logError("Option \"" + static_cast<std::string>(arg) + "\" requires -u or --unpack");
                throw 1;
            }
            value = args[++i];
            results.outName = value;
            break;
        }
    }
    // Default outDir or outName
    if (results.isArchive && results.outDir == "") {
        results.outDir = stripExt(static_cast<std::string>(results.fileName));
    }
    else if (results.isUnpack && results.outName == "") {
        results.outName = static_cast<std::string>(results.fileName) + ".dec";
    }
    // Default to raw output
    if (results.alg == algType::UNSPECIFIED) {
        results.alg = algType::NONE;
    }
    return results;
}
